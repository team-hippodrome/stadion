#!/bin/bash
# Deploys the web build (build_web/site, tools/build_web.sh) to a VM with nginx, behind a client certificate (mutual
# TLS): only browsers holding a certificate signed by the private CA get the page.
#
#   tools/deploy_web.sh cert      nginx: an HTTP server block for the host name, then a Let's Encrypt certificate
#   tools/deploy_web.sh site      upload the site and the CA certificate, nginx: the HTTPS server block (mTLS)
#   tools/deploy_web.sh verify    curl without the client certificate (must be refused) and with it (must work)
#
# Settings come from deploy.local.env (not in git; the host names stay out of the repository):
#   WEB_HOST=<host name of the site>         WEB_SITE=<short name: nginx config, /var/www/<name>>
#   GCP_PROJECT=<project>  GCP_ZONE=<zone>  GCP_INSTANCE=<instance>
#   CLIENT_CERT_DIR=<folder with ca.crt, client.crt, client.key>   LE_EMAIL=<address for Let's Encrypt, optional>
# Only the CA certificate (ca.crt) goes to the server, never a private key.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ENV_FILE="${DEPLOY_ENV:-$HERE/deploy.local.env}"
[ -f "$ENV_FILE" ] || { echo "missing $ENV_FILE (see the header of $0)"; exit 1; }
source "$ENV_FILE"
: "${WEB_HOST:?}" "${WEB_SITE:?}" "${GCP_PROJECT:?}" "${GCP_ZONE:?}" "${GCP_INSTANCE:?}" "${CLIENT_CERT_DIR:?}"
SITE_DIR="${WEB_SITE_DIR:-$HERE/build_web/site}"
ROOT="/var/www/$WEB_SITE"
CONF="/etc/nginx/sites-available/$WEB_SITE"
CA="/etc/nginx/$WEB_SITE-client-ca.crt"

remote() {
    gcloud compute ssh "$GCP_INSTANCE" --project "$GCP_PROJECT" --zone "$GCP_ZONE" --command "$1"
}

http_block() {
    cat <<EOF
# $WEB_SITE (tools/deploy_web.sh): ACME challenges, otherwise to HTTPS
server {
    listen 80;
    listen [::]:80;
    server_name $WEB_HOST;
    location / {
        return 301 https://\$host\$request_uri;
    }
}
EOF
}

https_block() {
    cat <<EOF

server {
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name $WEB_HOST;

    ssl_certificate /etc/letsencrypt/live/$WEB_HOST/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/$WEB_HOST/privkey.pem;
    include /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;

    # only clients with a certificate from the private CA
    ssl_client_certificate $CA;
    ssl_verify_client on;
    ssl_verify_depth 1;

    root $ROOT;
    index index.html;
    access_log /var/log/nginx/$WEB_SITE.access.log;
    error_log /var/log/nginx/$WEB_SITE.error.log;

    # the game's threads (SharedArrayBuffer) need a cross-origin isolated page
    add_header Cross-Origin-Opener-Policy "same-origin" always;
    add_header Cross-Origin-Embedder-Policy "require-corp" always;
    add_header Cross-Origin-Resource-Policy "same-origin" always;
    add_header X-Robots-Tag "noindex, nofollow" always;
    add_header Cache-Control "no-cache" always;

    gzip on;
    gzip_types application/wasm application/javascript text/javascript application/octet-stream text/css;

    location / {
        try_files \$uri \$uri/ =404;
    }
}
EOF
}

case "$1" in
cert)
    http_block > /tmp/$WEB_SITE.nginx.conf
    gcloud compute scp --project "$GCP_PROJECT" --zone "$GCP_ZONE" /tmp/$WEB_SITE.nginx.conf "$GCP_INSTANCE:/tmp/$WEB_SITE.nginx.conf"
    rm /tmp/$WEB_SITE.nginx.conf
    remote "set -e
        sudo install -m 644 /tmp/$WEB_SITE.nginx.conf $CONF && rm /tmp/$WEB_SITE.nginx.conf
        sudo ln -sf $CONF /etc/nginx/sites-enabled/$WEB_SITE
        sudo nginx -t && sudo systemctl reload nginx
        sudo certbot certonly --nginx --non-interactive --agree-tos ${LE_EMAIL:+-m $LE_EMAIL} ${LE_EMAIL:---register-unsafely-without-email} -d $WEB_HOST
        sudo test -f /etc/letsencrypt/live/$WEB_HOST/fullchain.pem && echo certificate ready"
    ;;
site)
    [ -f "$SITE_DIR/pokestadium.wasm" ] || { echo "no build in $SITE_DIR (tools/build_web.sh)"; exit 1; }
    { http_block; https_block; } > /tmp/$WEB_SITE.nginx.conf
    tar -C "$SITE_DIR" -czf /tmp/$WEB_SITE.site.tgz --exclude serve.py .
    gcloud compute scp --project "$GCP_PROJECT" --zone "$GCP_ZONE" /tmp/$WEB_SITE.nginx.conf /tmp/$WEB_SITE.site.tgz \
        "$CLIENT_CERT_DIR/ca.crt" "$GCP_INSTANCE:/tmp/"
    rm /tmp/$WEB_SITE.nginx.conf /tmp/$WEB_SITE.site.tgz
    remote "set -e
        sudo install -m 644 /tmp/ca.crt $CA && rm /tmp/ca.crt
        sudo rm -rf $ROOT.new && sudo mkdir -p $ROOT.new && sudo tar -C $ROOT.new -xzf /tmp/$WEB_SITE.site.tgz && rm /tmp/$WEB_SITE.site.tgz
        sudo chown -R root:root $ROOT.new && sudo chmod -R a+rX $ROOT.new
        sudo rm -rf $ROOT.old; if [ -d $ROOT ]; then sudo mv $ROOT $ROOT.old; fi; sudo mv $ROOT.new $ROOT
        sudo cp $CONF /tmp/$WEB_SITE.nginx.conf.bak 2>/dev/null || true
        sudo install -m 644 /tmp/$WEB_SITE.nginx.conf $CONF && rm /tmp/$WEB_SITE.nginx.conf
        sudo ln -sf $CONF /etc/nginx/sites-enabled/$WEB_SITE
        if sudo nginx -t; then sudo systemctl reload nginx; else sudo install -m 644 /tmp/$WEB_SITE.nginx.conf.bak $CONF; exit 1; fi"
    ;;
verify)
    url="https://$WEB_HOST/"
    without=$(curl -s -o /dev/null -w "%{http_code}" "$url" || true)
    with=$(curl -s -o /dev/null -w "%{http_code}" --cert "$CLIENT_CERT_DIR/client.crt" --key "$CLIENT_CERT_DIR/client.key" "$url")
    headers=$(curl -sI --cert "$CLIENT_CERT_DIR/client.crt" --key "$CLIENT_CERT_DIR/client.key" "${url}pokestadium.wasm" \
        | grep -iE "^(content-type|cross-origin-opener-policy|cross-origin-embedder-policy):" | tr -d '\r')
    echo "without client certificate: HTTP $without (want 400 or a refused handshake)"
    echo "with client certificate:    HTTP $with (want 200)"
    echo "$headers"
    [ "$with" = 200 ] && [ "$without" != 200 ]
    ;;
*)
    sed -n '2,15p' "$0"
    exit 1
    ;;
esac
