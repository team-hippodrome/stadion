// UI test for stadium_save/editor.html, run inside the macOS launcher's editor window by tests/test_ui.py:
//   LAUNCHER_EDITOR_TEST=tests/ui_test.js LAUNCHER_AUTO_CONFIRM=yes|no  (the launcher answers confirm() itself)
// This file is the body of an async function (WKWebView callAsyncJavaScript) with the argument `mode`
// ("yes": every control, then save; "no": the cancel answers of every confirmation). Every step clicks or changes the
// real page controls and checks the editor state. Returns a JSON report: {"steps": [[name, ok, info], ...], ...}.
const R = [];
const sleep = ms => new Promise(r => setTimeout(r, ms));
const main = () => document.getElementById("main");
async function until(fn, what, ms = 4000) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) { try { if (fn()) return; } catch (e) { /* not there yet */ } await sleep(25); }
  throw new Error("timeout waiting for " + what);
}
function assert(c, msg) { if (!c) throw new Error(msg); }
async function step(name, fn) {
  try { await fn(); R.push([name, true, ""]); } catch (e) { R.push([name, false, e.message]); }
}
const label0 = e => (e.childNodes[0] ? e.childNodes[0].textContent : e.textContent).trim();
function button(t, root) {
  const b = [...(root || document).querySelectorAll("button")].find(x => label0(x) === t && !x.closest("[hidden]"));
  assert(b, "no button " + JSON.stringify(t));
  return b;
}
function buttonStarts(t, root) {
  const b = [...(root || document).querySelectorAll("button")].find(x => label0(x).startsWith(t));
  assert(b, "no button starting " + JSON.stringify(t)); return b;
}
async function click(t, root) { const b = button(t, root); assert(!b.disabled, "button disabled: " + t); b.click(); await sleep(30); }
async function nav(name) {
  const a = [...document.querySelectorAll("nav a")].find(x => label0(x) === name);
  assert(a, "no nav link " + name); a.click(); await sleep(30);
}
function field(t, root) {
  const l = [...(root || main()).querySelectorAll("label")].find(x => x.textContent.trim() === t);
  assert(l, "no field " + JSON.stringify(t));
  const e = l.classList.contains("check") ? l.querySelector("input") : l.parentElement.querySelector("input, select");
  assert(e, "no control for " + t);
  return e;
}
async function set(t, v, root) {
  const e = field(t, root);
  if (e.type === "checkbox") e.checked = v; else e.value = v;
  e.dispatchEvent(new Event("change", { bubbles: true }));
  await sleep(30);
}
const dialog = () => document.getElementById("monDlg");
async function dlgSet(t, v) { await set(t, v, dialog()); }
async function dlgTab(t) { await click(t, dialog().querySelector(".head")); }
async function species(name) {
  const e = dialog().querySelector("input[list=speciesList]");
  e.value = name.toLowerCase(); e.dispatchEvent(new Event("change", { bubbles: true }));
  await until(() => dlg.preview && dlg.preview.species === name, "preview of " + name);
}
async function dlgFinish(t) {
  await click(t, dialog().querySelector(".foot"));
  await until(() => !dialog().open, "dialog to close");
  await sleep(30);
}
async function settle() { await until(() => !dlg || !dialog().open, "idle"); await sleep(60); }
function card(title) {
  const c = [...main().querySelectorAll(".card")].find(x => x.firstChild && x.firstChild.textContent === title);
  assert(c, "no card " + title); return c;
}
function monCards() { return [...main().querySelectorAll(".mon:not(.empty)")]; }
async function addCard(t) { const e = [...main().querySelectorAll(".mon.empty")].find(x => x.textContent === "+ " + t); assert(e, "no add card " + t); e.click(); await until(() => dialog().open, "dialog"); }
const status = () => document.getElementById("status").textContent;
const h2 = () => main().querySelector("h2").textContent;

if (mode === "no") {
  // every confirmation answered "Cancel": nothing may change
  await step("discard cancelled keeps changes", async () => {
    await nav("Options"); await click("Mono");
    await click("Discard changes"); assert(isDirty() && (cur.options.sound & 1), "changes were discarded");
  });
  await step("delete team cancelled", async () => {
    await nav("Registered teams"); await click("Poké Cup");
    view.slot = 0; render();
    const before = JSON.stringify(cur.teams.poke_cup[0]);
    await click("Delete team"); assert(JSON.stringify(cur.teams.poke_cup[0]) === before, "team changed");
  });
  await step("empty box cancelled", async () => {
    await nav("Pokémon Lab boxes"); const n = cur.boxes[0].entries.length;
    await click("Empty this box"); assert(cur.boxes[0].entries.length === n, "box emptied");
  });
  await step("remove all statues cancelled", async () => {
    await nav("Victory Palace"); const n = cur.victory_palace.entries.filter(Boolean).length;
    await click("Remove all"); assert(cur.victory_palace.entries.filter(Boolean).length === n, "statues removed");
  });
  await step("delete saved run cancelled", async () => {
    await nav("Saved cup run"); await click("Delete saved run"); assert(cur.continue.flags & 1, "run deleted");
  });
  await step("delete photos cancelled", async () => {
    await nav("Gallery"); const n = cur.album.entries.filter(Boolean).length;
    await click("Delete all", card("Photo album")); assert(cur.album.entries.filter(Boolean).length === n, "photos deleted");
  });
  await step("clear pokédex cancelled", async () => {
    await nav("Pokémon Gelb"); buttonStarts("Pokédex").click(); await sleep(30);
    const n = gbs[0].cur.pokedex.owned.length; await click("Clear Pokédex");
    assert(gbs[0].cur.pokedex.owned.length === n && n > 0, "pokédex cleared");
  });
  return JSON.stringify({ steps: R, dirty: isDirty() });  // left dirty: the launcher's close prompt is answered "Cancel"
}

// ---- mode "yes": every control -----------------------------------------------------------------------------------
await step("page loaded", async () => {
  assert(META && cur, "no save"); assert(status() === "All changes saved", status());
  assert(button("Save").disabled, "Save enabled without changes"); assert(button("Discard changes").disabled, "Discard enabled");
});
await step("every section opens", async () => {
  const want = { "Overview": "Overview", "Progress & unlocks": "Progress & unlocks", "Options": "Options",
    "Registered teams": "Registered teams", "Pokémon Lab boxes": "Pokémon Lab boxes", "Item boxes": "Item boxes",
    "Victory Palace": "Victory Palace", "Saved cup run": "Saved cup run", "Gallery": "Gallery", "Advanced": "Advanced" };
  for (const [n, h] of Object.entries(want)) { await nav(n); assert(h2() === h, n + " shows " + h2()); }
});
await step("overview: repair", async () => {
  await nav("Overview"); assert(health().length > 0, "fixture should have an unreadable record");
  await click("Repair"); await until(() => health().length === 0, "repair");
  assert(isDirty() && status() === "Unsaved changes", "repair not pending");
});
await step("overview: cards open their section", async () => {
  const cards = { "Round 1": "progress", "Round 2": "progress", "Registered teams": "teams", "Pokémon in the Lab": "boxes",
    "Victory Palace statues": "palace", "Saved cup run": "run" };
  for (const [t, sec] of Object.entries(cards)) { await nav("Overview"); card(t).click(); await sleep(30); assert(view.section === sec, t + " -> " + view.section); }
});
await step("overview: quick actions", async () => {
  await nav("Overview"); await click("Clear Round 1 & unlock Mewtwo");
  let p = cur.options.progress[0];
  assert((p.cleared_modes & 0xF8) === 0xF8 && p.mewtwo_unlocked && p.poke_cup === 4 && p.prime_cup === 4 && p.gym_leader_castle === 8, "round 1");
  await click("Clear both rounds"); p = cur.options.progress[1];
  assert((p.cleared_modes & 0xF8) === 0xF8 && p.mewtwo_unlocked, "round 2");
  await click("GB Tower: fastest speed"); assert(cur.options.gb_tower_speed === 2, "tower speed");
});
await step("discard changes", async () => {
  await click("Discard changes"); assert(!isDirty() && status() === "All changes saved", "still dirty");
  assert(health().length > 0, "repair should be discarded too");
});
await step("progress: checkboxes, selects, buttons", async () => {
  await nav("Progress & unlocks");
  const r1 = () => main().querySelectorAll(".card")[0], r2 = () => main().querySelectorAll(".card")[1];
  const bits = { "Poké Cup": 3, "Petit Cup": 4, "Pika Cup": 5, "Prime Cup": 6, "Gym Leader Castle": 7, "Beat Mewtwo": 8 };
  for (const [n, b] of Object.entries(bits)) {
    await set(n, true, r1()); assert(cur.options.progress[0].cleared_modes & (1 << b), n + " on");
    await set(n, false, r1()); assert(!(cur.options.progress[0].cleared_modes & (1 << b)), n + " off");
  }
  await set("Poké Cup divisions cleared", "2", r1()); assert(cur.options.progress[0].poke_cup === 2, "poke cup divisions");
  await set("Prime Cup divisions cleared", "3", r1()); assert(cur.options.progress[0].prime_cup === 3, "prime cup divisions");
  await set("Castle gyms cleared", "5", r1()); assert(cur.options.progress[0].gym_leader_castle === 5, "castle");
  await set("Mewtwo unlocked (night sky over the Castle)", true, r1()); assert(cur.options.progress[0].mewtwo_unlocked === 1, "mewtwo");
  await click("Clear everything", r2()); assert(cur.options.progress[1].gym_leader_castle === 8 && cur.options.progress[1].mewtwo_unlocked, "clear everything");
  await click("Reset", r2()); assert(cur.options.progress[1].cleared_modes === 0 && !cur.options.progress[1].mewtwo_unlocked, "reset");
  for (const [t, v] of [["Doduo (×2)", 1], ["Dodrio (×3)", 2], ["Normal", 0]]) { await click(t); assert(cur.options.gb_tower_speed === v, t); }
});
await step("options: every control", async () => {
  await nav("Options");
  await click("Mono"); assert(cur.options.sound & 1, "mono"); await click("Stereo"); assert(!(cur.options.sound & 1), "stereo");
  await click("Off"); assert(cur.options.sound & 2, "voice off"); await click("On"); assert(!(cur.options.sound & 2), "voice on");
  await set("Hyper difficulty unlocked", true); assert(cur.options.kids_club & 1, "hyper on");
  await set("Hyper difficulty unlocked", false); assert(!(cur.options.kids_club & 1), "hyper off");
  await set("Pokémon waiting", "151"); assert(cur.options.bonus_pokemon === 151, "bonus");
  await set("From Round 2", true); assert(cur.options.bonus_pokemon === 0x197, "bonus round 2");
  await set("Pokémon waiting", "0"); assert(cur.options.bonus_pokemon === 0, "bonus none");
});
await step("teams: tabs and slots", async () => {
  await nav("Registered teams");
  for (const [k, n] of CATS) { await click(n); assert(view.cat === k, n); }
  await click("Poké Cup");
  main().querySelectorAll(".slot")[2].click(); await sleep(30); assert(view.slot === 2, "slot 3");
});
await step("teams: name and trainer ID", async () => {
  await set("Team name (trainer)", "ash"); await set("Trainer ID", "4242");
  const t = cur.teams.poke_cup[2]; assert(t.name === "ASH" && t.trainer_id === 4242, JSON.stringify([t.name, t.trainer_id]));
});
await step("pokémon dialog: cancel adds nothing", async () => {
  await addCard("Add Pokémon"); await species("PIKACHU"); await dlgFinish("Cancel");
  assert(cur.teams.poke_cup[2].entries.length === 0, "added on cancel");
});
await step("pokémon dialog: every field", async () => {
  await addCard("Add Pokémon"); await species("PIKACHU");
  assert(dlg.d.nickname === "PIKACHU" && dlg.d.moves[0].move === "THUNDERSHOCK", "species defaults");
  await dlgSet("Nickname", "sparky"); await dlgSet("Level (1–100)", "55"); await dlgSet("Original trainer", "ash"); await dlgSet("Trainer ID (0–65535)", "4242");
  await dlgTab("Moves");
  const sel = () => [...dialog().querySelectorAll(".moverow")];
  let row = sel()[0].querySelectorAll("select"); row[0].value = "THUNDERBOLT"; row[0].dispatchEvent(new Event("change")); await sleep(30);
  row = sel()[2].querySelectorAll("select"); row[0].value = "SURF"; row[0].dispatchEvent(new Event("change")); await sleep(30);
  row = sel()[0].querySelectorAll("select"); row[1].value = "3"; row[1].dispatchEvent(new Event("change")); await sleep(30);
  assert(dlg.d.moves[0].move === "THUNDERBOLT" && dlg.d.moves[0].pp_up === 3 && dlg.d.moves[2].move === "SURF", "moves");
  await click("Use starting moves", dialog()); assert(dlg.d.moves[0].move === "THUNDERSHOCK" && !dlg.d.moves[2].move, "starting moves");
  row = sel()[0].querySelectorAll("select"); row[0].value = "THUNDERBOLT"; row[0].dispatchEvent(new Event("change")); await sleep(30);
  row = sel()[0].querySelectorAll("select"); row[1].value = "3"; row[1].dispatchEvent(new Event("change")); await sleep(30);
  await dlgTab("Stats");
  const num = [...dialog().querySelectorAll("table.stats input")];  // atk dv, hp se, ... (HP DV is computed)
  num[0].value = "15"; num[0].dispatchEvent(new Event("change")); await sleep(30);
  await click("Perfect stats", dialog()); assert(dlg.d.stat_exp.hp === 65535 && dlg.d.dvs.spc === 15, "perfect");
  await click("No training", dialog()); assert(dlg.d.stat_exp.atk === 0, "no training");
  const before = dlg.preview ? dlg.preview._info.stats.atk : 0;
  const atkDv = [...dialog().querySelectorAll("table.stats tr")][2].querySelector("input");
  atkDv.value = "3"; atkDv.dispatchEvent(new Event("change"));
  await until(() => dlg.preview._info.stats.atk < before, "stats to follow the DV");
  await dlgTab("Pokémon"); await dlgFinish("Add");
  const m = cur.teams.poke_cup[2].entries[0];
  assert(m && m.species === "PIKACHU" && m.nickname === "SPARKY" && m.level === 55 && m.ot_name === "ASH" && m.ot_id === 4242, JSON.stringify(m));
  assert(m.moves[0].move === "THUNDERBOLT" && m.moves[0].pp_up === 3 && m.moves[0].pp === 24 && m.dvs.atk === 3, JSON.stringify(m.moves));
  assert(teamRegistered(cur.teams.poke_cup[2]), "team not registered");
});
await step("pokémon dialog: edit and remove", async () => {
  await addCard("Add Pokémon"); await species("SNORLAX"); await dlgFinish("Add");
  assert(cur.teams.poke_cup[2].entries.length === 2, "second mon");
  monCards()[0].click(); await until(() => dialog().open, "dialog"); await dlgSet("Level (1–100)", "60"); await dlgFinish("Done");
  assert(cur.teams.poke_cup[2].entries[0].level === 60, "level edit");
  monCards()[1].click(); await until(() => dialog().open, "dialog"); await dlgFinish("Remove");
  assert(cur.teams.poke_cup[2].entries.length === 1, "remove");
});
await step("teams: delete team", async () => {
  await click("Delete team"); const t = cur.teams.poke_cup[2];
  assert(t.flags === 0 && t.entries.length === 0 && t.name === "", JSON.stringify(t));
});
await step("boxes: navigation", async () => {
  await nav("Pokémon Lab boxes");
  await click("▶"); assert(view.box === 1, "next"); await click("◀"); assert(view.box === 0, "prev");
  await click("◀"); assert(view.box === 11, "wrap"); await click("▶"); assert(view.box === 0, "wrap back");
  const s = main().querySelector(".boxnav select"); s.value = "4"; s.dispatchEvent(new Event("change")); await sleep(30); assert(view.box === 4, "select");
});
await step("boxes: add, edit, remove, empty", async () => {
  await addCard("Add"); await species("BULBASAUR"); await dlgFinish("Add");
  await addCard("Add"); await species("CHARMANDER"); await dlgFinish("Add");
  assert(cur.boxes[4].entries.length === 2, "two in box");
  monCards()[0].click(); await until(() => dialog().open, "dialog"); await dlgSet("Nickname", "bulby"); await dlgFinish("Done");
  assert(cur.boxes[4].entries[0].nickname === "BULBY", "nickname");
  monCards()[1].click(); await until(() => dialog().open, "dialog"); await dlgFinish("Remove");
  assert(cur.boxes[4].entries.length === 1, "remove");
  await click("Empty this box"); assert(cur.boxes[4].entries.length === 0, "empty");
});
await step("items: tabs, add, change, remove, empty", async () => {
  await nav("Item boxes");
  for (let i = 4; i >= 1; i--) { await click("Item box " + i); assert(view.ibox === i - 1, "tab " + i); }
  await click("Item box 2"); await click("+ Add item"); await click("+ Add item");
  let b = cur.item_boxes[1]; assert(b.entries.length === 2 && b.entries[0].item === 40 && b.name === "RED" && (b.flags & 1), JSON.stringify(b));
  const rows = () => [...main().querySelectorAll("table.items tr")];
  let s = rows()[0].querySelector("select"); s.value = "1"; s.dispatchEvent(new Event("change")); await sleep(30);
  let q = rows()[0].querySelector("input"); q.value = "99"; q.dispatchEvent(new Event("change")); await sleep(30);
  assert(cur.item_boxes[1].entries[0].item === 1 && cur.item_boxes[1].entries[0].quantity === 99, "item/qty");
  rows()[1].querySelector("button").click(); await sleep(30); assert(cur.item_boxes[1].entries.length === 1, "✕");
  await set("Owner name", "misty"); await set("Owner ID", "777");
  assert(cur.item_boxes[1].name === "MISTY" && cur.item_boxes[1].trainer_id === 777, "owner");
  await click("Empty this box"); b = cur.item_boxes[1]; assert(b.entries.length === 0 && b.flags === 0, "empty");
});
await step("victory palace: statue fields", async () => {
  await nav("Victory Palace");
  const tile = n => [...main().querySelectorAll(".dex")].find(x => x.lastChild.textContent === n);
  tile("MEW").click(); await sleep(30); assert(view.dex === 151, "tile");
  await click("Add statue"); assert(cur.victory_palace.entries[150], "added");
  await set("Nickname", "mewmew"); await set("Level", "70"); await set("Won in", "6"); await set("Round 2", true);
  await set("Trainer", "ash"); await set("Trainer ID", "4242");
  const e = cur.victory_palace.entries[150];
  assert(e.nickname === "MEWMEW" && e.level === 70 && e.mode === 0x86 && e.ot_name === "ASH" && e.ot_id === 4242, JSON.stringify(e));
  await click("Remove statue"); assert(cur.victory_palace.entries[150] === null, "removed");
});
await step("victory palace: all / none", async () => {
  await click("Give every Pokémon a statue"); assert(cur.victory_palace.entries.every(Boolean), "all");
  await click("Remove all"); assert(!cur.victory_palace.entries.some(Boolean), "none");
});
await step("saved run: fields and team", async () => {
  await nav("Saved cup run"); assert(cur.continue.flags & 1, "fixture run");
  await set("Where", "6"); assert(cur.continue.session.mode === 6, "mode");
  await set("Division", "2"); await set("Next battle", "5"); await set("Continues used", "2");
  await set("Player name", "gary"); await set("Trainer ID", "1234"); await set("Round 2", true);
  const se = cur.continue.session;
  assert(se.mode_selection === 2 && se.battle === 5 && se.continues === 2 && se.player_name === "GARY" && se.trainer_id === 1234 && se.round2 === 1, JSON.stringify(se));
  await set("Where", "7"); await set("Stage", "8"); assert(cur.continue.session.mode_selection === 8, "castle stage");
  const n = cur.continue.count;
  monCards()[0].click(); await until(() => dialog().open, "dialog"); await dlgSet("Level (1–100)", "52"); await dlgFinish("Done");
  assert(cur.continue.pokemon[0].level === 52, "edit");
  monCards()[n - 1].click(); await until(() => dialog().open, "dialog"); await dlgFinish("Remove");
  assert(cur.continue.count === n - 1, "remove");
  await addCard("Add Pokémon"); await species("GENGAR"); await dlgFinish("Add");
  assert(cur.continue.count === n && cur.continue.pokemon[n - 1].species === "GENGAR", "add");
});
await step("saved run: delete and create", async () => {
  await click("Delete saved run"); assert(!(cur.continue.flags & 1) && cur.continue.count === 0, "deleted");
  await click("Create a saved run"); assert(cur.continue.flags & 1 && cur.continue.session.battle === 1, "created");
  assert(main().textContent.includes("needs at least one Pokémon"), "warning");
  await addCard("Add Pokémon"); await species("MEWTWO"); await dlgFinish("Add"); assert(cur.continue.count === 1, "mon");
});
await step("gallery: delete photos", async () => {
  await nav("Gallery"); assert(cur.album.entries.some(Boolean) && cur.photos.entries.some(Boolean), "fixture photos");
  await click("Delete all", card("Photo album")); assert(!cur.album.entries.some(Boolean), "album");
  await click("Delete all", card("Current photo set")); assert(!cur.photos.entries.some(Boolean), "photo set");
});
await step("advanced: record JSON", async () => {
  await nav("Advanced");
  const s = main().querySelector("select"); s.value = "options"; s.dispatchEvent(new Event("change")); await sleep(30);
  const ta = main().querySelector("textarea"); const o = JSON.parse(ta.value); o.gb_tower_word = 7; ta.value = JSON.stringify(o);
  await click("Apply"); assert(cur.options.gb_tower_word === 7, "apply");
});
// ---- Game Boy game in the Transfer Pak (fixture: German Yellow, party PIKACHU BULBASAUR CHARMANDER) ----------------
const G = () => gbs[0].cur;
await step("gb: section and tabs", async () => {
  assert(gbs.length === 1 && gbs[0].lang === "de", "no GB save");
  await nav("Pokémon Gelb"); assert(h2().includes("Pokémon Gelb"), h2());
  for (const t of ["Trainer", "Party", "PC boxes", "Items", "Pokédex"]) { buttonStarts(t).click(); await sleep(30); }
  assert(view.gbTab === "dex", view.gbTab);
});
await step("gb: trainer fields and badges", async () => {
  buttonStarts("Trainer").click(); await sleep(30);
  await set("Player name", "ash"); await set("Rival name", "blue"); await set("Trainer ID", "12345");
  await set("Money (₽)", "999999"); await set("Pikachu's friendship (0–255)", "255");
  const t = G().trainer;
  assert(t.name === "ASH" && t.rival === "BLUE" && t.id === 12345 && t.money === 999999 && t.pikachu_happiness === 255, JSON.stringify(t));
  await set("Boulder Badge", true); assert(t.badges === 1, "boulder");
  await click("All badges"); assert(G().trainer.badges === 255, "all");
  await click("No badges"); assert(G().trainer.badges === 0, "none");
  await set("Cascade Badge", true); assert(G().trainer.badges === 2, "cascade");
});
await step("gb: party add, edit, remove", async () => {
  buttonStarts("Party").click(); await sleep(30);
  assert(G().party.length === 3, "fixture party");
  await addCard("Add"); await species("ONIX");
  assert(dlg.d.ot_name === "ASH" && dlg.d.ot_id === 12345, "OT defaults to the trainer");
  await dlgFinish("Add"); assert(G().party.length === 4 && G().party[3].species === "ONIX", "add");
  monCards()[0].click(); await until(() => dialog().open, "dialog"); await dlgSet("Level (1–100)", "30"); await dlgFinish("Done");
  assert(G().party[0].level === 30, "edit");
  monCards()[3].click(); await until(() => dialog().open, "dialog"); await dlgFinish("Remove"); assert(G().party.length === 3, "remove");
});
await step("gb: PC boxes", async () => {
  buttonStarts("PC boxes").click(); await sleep(30);
  const sel = main().querySelector(".boxnav select"); sel.value = "2"; sel.dispatchEvent(new Event("change")); await sleep(30);
  assert(view.gbBox === 2, "select");
  await addCard("Add"); await species("MEW"); await dlgFinish("Add"); assert(G().boxes[2].length === 1, "add");
  await click("▶"); assert(view.gbBox === 3, "next");
  await addCard("Add"); await species("DITTO"); await dlgFinish("Add");
  await click("Empty this box"); assert(G().boxes[3].length === 0, "empty");
  await click("◀"); assert(view.gbBox === 2, "prev");
});
await step("gb: bag and PC items", async () => {
  buttonStarts("Items").click(); await sleep(30);
  const cards = () => main().querySelectorAll(".card");
  await click("+ Add item", cards()[0]); assert(G().bag.length === 1, "bag add");
  let s = cards()[0].querySelector("select"); s.value = "20"; s.dispatchEvent(new Event("change")); await sleep(30);
  let q = cards()[0].querySelector("input"); q.value = "5"; q.dispatchEvent(new Event("change")); await sleep(30);
  assert(G().bag[0].item === 20 && G().bag[0].quantity === 5, JSON.stringify(G().bag));
  const n = G().pc_items.length;
  await click("+ Add item", cards()[1]); assert(G().pc_items.length === n + 1, "pc add");
  [...cards()[1].querySelectorAll("button.link")].pop().click(); await sleep(30); assert(G().pc_items.length === n, "pc remove");
});
await step("gb: pokédex", async () => {
  buttonStarts("Pokédex").click(); await sleep(30);
  const tile = () => [...main().querySelectorAll(".dex")].find(x => x.lastChild.textContent === "MEW");
  const d = G().pokedex;
  tile().click(); await sleep(30); assert(G().pokedex.seen.includes(151) && !G().pokedex.owned.includes(151), "seen");
  tile().click(); await sleep(30); assert(G().pokedex.owned.includes(151), "caught");
  tile().click(); await sleep(30); assert(!G().pokedex.owned.includes(151) && !G().pokedex.seen.includes(151), "none");
  await click("Mark all caught"); assert(G().pokedex.owned.length === 151, "all");
  await click("Clear Pokédex"); assert(G().pokedex.owned.length === 0 && G().pokedex.seen.length === 0, "clear");
  await click("Mark all caught");
});
await step("lab boxes: move Pokémon between the Lab and the Game Boy", async () => {
  await nav("Pokémon Lab boxes"); view.box = 0; view.xstore = "party"; render(); await sleep(30);
  const panes = () => main().querySelectorAll(".pair > .card");
  const moveBtn = (pane, name) => [...panes()[pane].querySelectorAll(".mon")].find(m => m.querySelector(".name") && m.querySelector(".name").textContent === name).querySelector("button.move");
  assert(cur.boxes[0].entries.map(m => m.species).join() === "EEVEE", "lab fixture");
  moveBtn(1, "PIKACHU").click(); await sleep(30);
  assert(G().party.map(m => m.species).join() === "BULBASAUR,CHARMANDER", "party after");
  const moved = cur.boxes[0].entries[1];
  assert(moved.species === "PIKACHU" && typeof moved.nickname === "string" && moved.level === 30, JSON.stringify(moved.nickname));
  const sel = panes()[1].querySelectorAll("select"); const store = sel[sel.length - 1];
  store.value = "0"; store.dispatchEvent(new Event("change")); await sleep(30); assert(view.xstore === 0, "store");
  moveBtn(0, "EEVEE").click(); await sleep(30);
  assert(G().boxes[0].map(m => m.species).join() === "EEVEE" && cur.boxes[0].entries.map(m => m.species).join() === "PIKACHU", "to GB box");
  view.xstore = "party"; render(); await sleep(30);
  moveBtn(1, "BULBASAUR").click(); await sleep(30);
  assert(G().party.length === 1 && moveBtn(1, "CHARMANDER").disabled, "the last party Pokémon must stay");
  assert(isDirty() && gbDirty(gbs[0]), "GB not dirty");
});
// final state the Python side checks in the written file
await step("final edits", async () => {
  await nav("Overview"); await click("Repair"); await until(() => health().length === 0, "repair");
  await click("Clear Round 1 & unlock Mewtwo");
  await nav("Options"); await click("Mono");
  await nav("Registered teams"); await click("Poké Cup"); main().querySelectorAll(".slot")[0].click(); await sleep(30);
  await set("Team name (trainer)", "ash"); await set("Trainer ID", "4242");
  await addCard("Add Pokémon"); await species("MEWTWO"); await dlgSet("Level (1–100)", "55"); await dlgFinish("Add");
  await nav("Pokémon Lab boxes"); view.box = 0; render(); await addCard("Add"); await species("MEW"); await dlgFinish("Add");
  await nav("Item boxes"); await click("Item box 1"); await click("+ Add item");
  await nav("Victory Palace"); [...main().querySelectorAll(".dex")].find(x => x.lastChild.textContent === "PIKACHU").click(); await sleep(30);
  await click("Add statue");
});
await step("save button writes the file", async () => {
  assert(status() === "Unsaved changes" && !button("Save").disabled, "not dirty");
  await click("Save"); await until(() => !isDirty() && status() === "All changes saved", "save");
  assert(base.options.sound & 1, "saved state");
});
await step("cmd+S saves", async () => {
  await nav("Options"); await click("Stereo"); await click("Off"); assert(isDirty(), "dirty");
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "s", metaKey: true, bubbles: true }));
  await until(() => !isDirty(), "cmd+S");
});
await step("unsaved change left for the close prompt", async () => { await click("Mono"); assert(isDirty(), "dirty"); });
return JSON.stringify({ steps: R, dirty: isDirty() });
