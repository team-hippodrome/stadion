#!/bin/bash
# Deploys the web build (build_web/site, tools/build_web.sh) to a VM with nginx, behind a password (HTTP basic
# authentication over HTTPS) or a client certificate (mutual TLS: only browsers holding a certificate signed by the
# private CA get the page).
#
#   tools/deploy_web.sh cert      nginx: an HTTP server block for the host name, then a Let's Encrypt certificate
#   tools/deploy_web.sh site      upload the site and the password hash or CA certificate, nginx: the HTTPS server block
#   tools/deploy_web.sh verify    curl without the password/certificate (must be refused) and with it (must work)
#
# Settings come from deploy.local.env (not in git; the host names stay out of the repository):
#   WEB_HOST=<host name of the site>         WEB_SITE=<short name: nginx config, /var/www/<name>>
#   GCP_PROJECT=<project>  GCP_ZONE=<zone>  GCP_INSTANCE=<instance>  LE_EMAIL=<address for Let's Encrypt, optional>
#   WEB_AUTH=password (default) or cert
#   password: WEB_PASSWORD_FILE=<file holding the password>  WEB_USER=<user name, default stadium>
#   cert:     CLIENT_CERT_DIR=<folder with ca.crt, client.crt, client.key>
# Only a hash of the password (SHA-512 crypt) or the CA certificate (ca.crt) goes to the server.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ENV_FILE="${DEPLOY_ENV:-$HERE/deploy.local.env}"
[ -f "$ENV_FILE" ] || { echo "missing $ENV_FILE (see the header of $0)"; exit 1; }
source "$ENV_FILE"
: "${WEB_HOST:?}" "${WEB_SITE:?}" "${GCP_PROJECT:?}" "${GCP_ZONE:?}" "${GCP_INSTANCE:?}"
WEB_AUTH="${WEB_AUTH:-password}"
WEB_USER="${WEB_USER:-stadium}"
case "$WEB_AUTH" in
password) : "${WEB_PASSWORD_FILE:?}" ;;
cert) : "${CLIENT_CERT_DIR:?}" ;;
*) echo "WEB_AUTH must be password or cert"; exit 1 ;;
esac
SITE_DIR="${WEB_SITE_DIR:-$HERE/build_web/site}"
ROOT="/var/www/$WEB_SITE"
CONF="/etc/nginx/sites-available/$WEB_SITE"
CA="/etc/nginx/$WEB_SITE-client-ca.crt"
HTPASSWD="/etc/nginx/$WEB_SITE.htpasswd"

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

auth_lines() {
    if [ "$WEB_AUTH" = cert ]; then
        cat <<EOF
    # only clients with a certificate from the private CA
    ssl_client_certificate $CA;
    ssl_verify_client on;
    ssl_verify_depth 1;
EOF
    else
        cat <<EOF
    # only with the password (the browser asks once and sends it along with every request)
    auth_basic "Stadium";
    auth_basic_user_file $HTPASSWD;
EOF
    fi
}

# the game's versioned files (name.<hash>.ext, tools/build_web.sh): kept by browsers for a year, as the Brotli copy
# (.br) for browsers that take it (precompressed; no nginx module needed)
asset_location() { # <extension> <content type>
    cat <<EOF
    location ~ "\.[0-9a-f]{12}\.$1\$" {
        types { }
        default_type $2;
        gzip off;
        add_header Content-Encoding \$${WEB_SITE}_br_encoding always;
        add_header Vary Accept-Encoding always;
        add_header Cache-Control "public, max-age=31536000, immutable" always;
        add_header Cross-Origin-Opener-Policy "same-origin" always;
        add_header Cross-Origin-Embedder-Policy "require-corp" always;
        add_header Cross-Origin-Resource-Policy "same-origin" always;
        add_header X-Robots-Tag "noindex, nofollow" always;
        try_files \$uri\$${WEB_SITE}_br_suffix =404;
    }
EOF
}

https_block() {
    cat <<EOF

map \$http_accept_encoding \$${WEB_SITE}_br_suffix {
    default "";
    "~*\bbr\b" ".br";
}
map \$http_accept_encoding \$${WEB_SITE}_br_encoding {
    default "";
    "~*\bbr\b" "br";
}

server {
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name $WEB_HOST;

    ssl_certificate /etc/letsencrypt/live/$WEB_HOST/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/$WEB_HOST/privkey.pem;
    include /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;

$(auth_lines)

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
$(asset_location js text/javascript)
$(asset_location wasm application/wasm)
$(asset_location o2r application/octet-stream)
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
    ls "$SITE_DIR"/pokestadium.*.wasm.br >/dev/null 2>&1 || { echo "no build in $SITE_DIR (tools/build_web.sh, with brotli)"; exit 1; }
    { http_block; https_block; } > /tmp/$WEB_SITE.nginx.conf
    COPYFILE_DISABLE=1 tar --no-xattrs -C "$SITE_DIR" -czf /tmp/$WEB_SITE.site.tgz --exclude serve.py .
    if [ "$WEB_AUTH" = cert ]; then
        cp "$CLIENT_CERT_DIR/ca.crt" /tmp/$WEB_SITE.auth
        install_auth="sudo install -m 644 /tmp/$WEB_SITE.auth $CA"
    else
        printf '%s:%s\n' "$WEB_USER" "$(openssl passwd -6 -stdin < "$WEB_PASSWORD_FILE")" > /tmp/$WEB_SITE.auth
        install_auth="sudo install -m 640 -o root -g www-data /tmp/$WEB_SITE.auth $HTPASSWD"
    fi
    gcloud compute scp --project "$GCP_PROJECT" --zone "$GCP_ZONE" /tmp/$WEB_SITE.nginx.conf /tmp/$WEB_SITE.site.tgz \
        /tmp/$WEB_SITE.auth "$GCP_INSTANCE:/tmp/"
    rm /tmp/$WEB_SITE.nginx.conf /tmp/$WEB_SITE.site.tgz /tmp/$WEB_SITE.auth
    remote "set -e
        $install_auth && rm /tmp/$WEB_SITE.auth
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
    if [ "$WEB_AUTH" = cert ]; then
        cred=(--cert "$CLIENT_CERT_DIR/client.crt" --key "$CLIENT_CERT_DIR/client.key")
        wrong=()
    else
        cred=(-K -)
        wrong=(-u "$WEB_USER:not-the-password")
    fi
    get() { printf 'user = "%s:%s"\n' "$WEB_USER" "$(cat "${WEB_PASSWORD_FILE:-/dev/null}")" | curl -s "$@"; }
    without=$(curl -s -o /dev/null -w "%{http_code}" "$url" || true)
    with=$(get -o /dev/null -w "%{http_code}" "${cred[@]}" "$url")
    wasm=$(get "${cred[@]}" "${url}port.js" | grep -o 'pokestadium\.[0-9a-f]*\.wasm' | head -1)
    headers=$(get -I -H "Accept-Encoding: br, gzip" "${cred[@]}" "${url}$wasm" \
        | grep -iE "^(content-type|content-encoding|cache-control|cross-origin-opener-policy|cross-origin-embedder-policy):" \
        | tr -d '\r')
    echo "$wasm:"
    echo "without $WEB_AUTH: HTTP $without (want 401/400 or a refused handshake)"
    if [ ${#wrong[@]} -gt 0 ]; then
        echo "wrong password:   HTTP $(curl -s -o /dev/null -w "%{http_code}" "${wrong[@]}" "$url") (want 401)"
    fi
    echo "with $WEB_AUTH:    HTTP $with (want 200)"
    echo "$headers"
    [ "$with" = 200 ] && [ "$without" != 200 ]
    ;;
*)
    sed -n '2,19p' "$0"
    exit 1
    ;;
esac
