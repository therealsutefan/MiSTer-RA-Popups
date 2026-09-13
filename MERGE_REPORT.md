# MERGE_REPORT.md — MainMister_new = odelot NEU + Custom-Patches aus MainMister_old

Erzeugt: 2026-09-07. Verfahren (ordnerbasiert, kein Git): `MainMister_new` = vollständige Kopie von
`MainMister_odelot` (504 Dateien, 1:1 identisch zur Kopie), anschließend wurden alle Custom-Änderungen
aus `MainMister_old` einzeln dateiweise reapplied/reconciled. Basis der Klassifizierung: vollständiger
`diff -rq` zwischen `MainMister_old` und `MainMister_odelot` (ohne `bin/`, `releases/` — reine
Build-Artefakte) — **7 abweichende Quelldateien**, keine neuen/gelöschten Dateien sonst. Es wurde
**nicht gebaut und nicht deployed**, wie angewiesen.

---

## 0. Odelot-Versionsnummer

Im gesamten Baum (`MainMister_odelot`) existiert **keine explizite Versionskennung** (kein `version.h`,
kein Versions-String im README, kein CHANGELOG-Eintrag) — das war schon bei der letzten Merge-Runde so
(damals referenziert als "MainMister18", intern nie mit einer Versionsnummer im Code markiert; die
`1.12.1` war nur der Dateiname des mitgelieferten Release-Zips `Main_Mister-v1.12.1.zip`, das in diesem
neuen Release **nicht mehr enthalten** ist — es gibt kein Äquivalent-Zip in `MainMister_odelot`). Die
`releases/`-Unterordner (historische Stock-MiSTer-Firmware-Images 2018–2019) sind byte-identisch in
beiden Bäumen und damit kein Versionsindikator für den odelot-Fork selbst.

Die "Frische" des Release lässt sich daher nur am tatsächlichen Code-Diff ablesen (Abschnitt 1).

---

## 1. Odelot-Neuerungen (neu gegenüber MainMister_old, nicht-custom — 1:1 aus `_odelot` übernommen)

Diese Dateien hat odelot in diesem Release neu geändert. Sie sind in `MainMister_new` unverändert aus
`MainMister_odelot` vorhanden (kein Custom-Override), da sie außerhalb der dokumentierten Custom-Patches
liegen bzw. sich sauber davon trennen ließen:

- **`user_io.cpp`**: neuer UART-Modus 6 (`/tmp/uartmode6` → `return 6`), plus ein `dosend = 0`-Fix nach
  dem ROM-Load-Zweig in der UART-Sende-Logik (verhindert vermutlich ein doppeltes/verzögertes Senden
  nach `free(rom)`).
- **`menu.cpp`**: `config_uart_msg[]` um `"UDP"` und `"SNI"` erweitert; UART-Auswahlmenü zeigt UDP nur
  bei aktivem UDP-Modus, SNI nur bei aktivem SNI-Modus oder wenn ein SNES-Core läuft **und**
  `/media/fat/snid` existiert (neue Helper-Logik `udp_enabled`/`sni_enabled`/`skipped` samt
  Skip-Loop beim Rendern der Menüzeilen). Kollidiert nicht mit dem Custom-`InfoAligned`-Patch (andere
  Funktion, andere Zeilen) — beides gleichzeitig in `MainMister_new` vorhanden, siehe Abschnitt 2.
- **`video.cpp`**: `dac_from_edid()`-artige Funktion erweitert — bei ungültigem EDID-Header wird jetzt,
  falls vorhanden, auf eine zuvor separat erfasste "raw manufacturer ID" (`raw_edid_mfg_id`/
  `raw_edid_mfg_id_valid`) zur DAC-Erkennung zurückgegriffen, statt sofort `return 0` zu liefern. Das
  ist ein reines odelot-EDID-Feature, **kein** QMTech-Patch (siehe Abschnitt 5 zur expliziten
  Verifikation, dass kein QMTech/EDID-Custom-Code existiert).
- **`README.md`**: Core-Support-Tabelle umformatiert (Wording `✅ Officially Supported` → `✅ Supported`,
  `🔧 wired, in validation` → `HC wired, pending validation`) **und** Gameboy/GBC von "wired, in
  validation" zu "✅ Supported" hochgestuft (Hardcore-Guardrails für GB/GBC jetzt offiziell validiert).
  Siehe Abschnitt 3 — dies ist der zentrale Anti-Trap-Fall dieses Merges.
- **`achievements_gameboy.cpp`**: keine Änderung nötig — `hardcore_protected` steht in `MainMister_old`
  bereits auf `1` (odelot-Wert), identisch zu `MainMister_odelot`. Die im Auftrag beschriebene
  "veraltete 0" aus einem früheren Merge-Lauf ist in diesem alten Fork-Stand bereits korrigiert; die
  Datei erschien im `diff -rq` gar nicht als abweichend.
- **`support/snes/snes.cpp` / `snes.h`**: bereits byte-identisch zu `_odelot` (kein Diff) — der
  SNAC/Joystick-Totcode (`snes_get_joystick`, `last_joy1`/`2`, `case 0x37`) ist nachweislich **nicht**
  vorhanden. Bestätigt in Abschnitt 5.

Keine Kollision mit den Custom-Patches in diesen Bereichen, mit der einen dokumentierten Ausnahme in
Abschnitt 2 (`menu.cpp`, zwei unabhängige Änderungen in derselben Datei, aber unterschiedliche
Funktionen).

---

## 2. Custom-Dateien: was reapplied wurde

### `achievements.cpp`
`MainMister_odelot/achievements.cpp` (2913 Zeilen) → `MainMister_new/achievements.cpp` (3228 Zeilen,
identisch zu `MainMister_old`). **Vollständiger Diff `_old` vs. `_odelot` ergab 19 Hunks — alle 19 ohne
Ausnahme sind Custom-Code, odelot hat an dieser Datei in diesem Release nichts geändert** (die
odelot-eigenen Diagnose-Calls `gba_dump_trigger(...)` / `seladdr_trigdump_report(...)` stehen in jedem
betroffenen Hunk unverändert als Kontextzeilen — sie existieren identisch in beiden Bäumen). Deshalb
wurde die Datei komplett aus `MainMister_old` übernommen, das entspricht exakt "odelot NEU + alle
Custom-Hunks reapplied", verifiziert per `diff -q` (keine Abweichung zu `_old`). Enthalten:

1. `g_popup_h_offset` + `g_popup_v_offset` (Deklaration, Reset-Defaults, Parsing der Keys
   `popup_h_offset`/`popup_v_offset`).
2. `[CORE]`-Section-Parsing: `ra_cfg_section_matches()`, Section-Active-Tracking in
   `ra_load_credentials()`, plus die abgeleitete Funktion `ra_load_popup_settings()` (re-evaluiert die
   drei Popup-Placement-Keys pro Core-Load statt nur einmal beim Boot) samt ihrem Aufruf in
   `achievements_load_game()`.
3. `CHALLENGE_POPUP_COOLDOWN_SEC = 15` (odelot-Default in diesem Release weiterhin `10`).
4. `ra_notify_progress()`-Dauer `1000ms` (odelot-Default weiterhin `2500ms`).
5. `word_wrap_split()`-Helper.
6. `RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED`: komplette Popup-Neuformatierung — kein
   `>> ACHIEVEMENT <<`-Header, Titel-Wrap auf bis zu 2 Zeilen, Beschreibung auf bis zu 3/2 Zeilen,
   Punkte-Suffix `[+X]`, dynamische Dauer 5000/6000/7000/8000ms nach Punktwert. `gba_dump_trigger(...)`
   und `seladdr_trigdump_report(...)` bleiben unverändert als erste beiden Statements erhalten.
7. `CHALLENGE_INDICATOR_SHOW`: `[A]`-Präfix, Word-Wrap-Beschreibung, Progress-Zeile, über
   `ra_notify_urgent()` statt `ra_notify()`, 6000ms.
8. `CHALLENGE_INDICATOR_HIDE`: `[F]`-Präfix (kein `[P]`), Abbruch bei
   `RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED`, sonst analog SHOW.
9. `PROGRESS_INDICATOR_SHOW/UPDATE`: Titel-Word-Wrap (2 Zeilen) statt fixer Kappung.
10. `GAME_COMPLETED`: `ra_notify_urgent(..., 5000, 1)` statt `ra_notify_urgent(..., 5000)` +
    separatem `ra_play_achievement_sound()` — Doppel-Sound-Fix. Odelots `ra_notify_urgent()`-Signatur
    selbst (der `play_sound`-Parameter) ist bereits Teil von odelots eigenem Code in diesem Release
    (Zeile 519, auch von odelot selbst am Trigger-Event mit `1` aufgerufen) — hier musste nur die
    **Aufrufstelle** in `GAME_COMPLETED` angepasst werden, keine Signatur-Änderung nötig.
11. 5 `InfoAligned(...)`-Call-Sites um `g_popup_h_offset, g_popup_v_offset` erweitert (2× im
    OSD-Poll, 3× in `achievements_info()`).

**Hinweis zu `popup_v_offset`:** Im Auftrag ist explizit nur `popup_h_offset` als Custom-Config-Key und
nur "der h_offset-Parameter" für `menu.cpp`/`menu.h` gelistet. Im tatsächlichen Code von `MainMister_old`
existiert `popup_v_offset` aber als vollständig ausgebautes, funktionierendes Zwillingsfeature (eigener
Config-Key, eigene Doku im `.cfg`, [CORE]-Section-fähig, identische Behandlung wie `h_offset`). Da es
sich um dieselbe zusammenhängende Popup-Placement-Funktionalität handelt und nichts davon wie ein
veralteter odelot-Default aussieht (odelot kennt `v_offset` gar nicht), wurde es als Teil desselben
Custom-Features mit übernommen, nicht der Anti-Trap-Regel geopfert. **Bitte gegenprüfen, ob das so
gewollt ist** — es ist keine Erfindung meinerseits, sondern bereits vorhandener Code aus `MainMister_old`.

### `scaler.cpp` (4 Compile-/Bug-Fixes)
**Geprüft, ob odelot diese Bugs in diesem Release selbst gefixt hat: Nein**, alle vier sind in
`MainMister_odelot` unverändert vorhanden. Patch komplett reapplied (Datei 1:1 aus `_old` übernommen,
verifiziert per `diff -q` — keine sonstigen Abweichungen zu `_odelot` außer genau diesen vier Stellen):
1. Duplicate-Default-Argument: odelot deklariert weiterhin `format = RGB` in der `.cpp`-Definition,
   obwohl `scaler.h` bereits `format = ARGB32` deklariert. Fix: Default in der `.cpp`-Definition entfernt.
2. Undeklariertes `limit` im Non-NEON-Scalar-Pfad (`for (int x = limit; ...)` im ARGB32-Case) — odelot
   hat das nicht behoben. Fix: `x = limit` → `x = 0`.
3./4. `errno`-Namenskollision (`struct { ...; Imlib_Load_Error errno; }`, schattiert das globale
   `errno`-Makro) — weiterhin vorhanden in odelot. Fix: Feld zu `errcode` umbenannt, inkl. der
   Usage-Site in `print_imlib_load_error()`.

`scaler.h` ist in diesem Release weiterhin identisch zwischen `_old` und `_odelot` — keine Änderung nötig.

### `menu.cpp` / `menu.h`
**Echte Kollision, siehe Abschnitt 3** — odelot hat in diesem Release das UART-Auswahlmenü erweitert
(UDP/SNI-Einträge, Abschnitt 1), an einer völlig anderen Stelle als mein `InfoAligned`-Patch. Beide
wurden kombiniert:
- `menu.h`: `InfoAligned()`-Deklaration um `int h_offset = 0, int v_offset = 0` erweitert (1:1 aus
  `_old`, einzige Änderung in dieser Datei — Datei ist sonst identisch zu `_old`).
- `menu.cpp`: odelots UART-UDP/SNI-Erweiterung (`config_uart_msg[]`, `udp_enabled`/`sni_enabled`-Logik)
  unverändert aus `_odelot` übernommen; **zusätzlich** `InfoAligned()`-Definition um `h_offset`/
  `v_offset` erweitert (`x`-Berechnung nutzt `h_offset` bei `LEFT`/`RIGHT`, clampt `x` zusätzlich nach
  oben; `y`-Berechnung nutzt `v_offset`, geclampt auf `>= 0`) — identisch zur Logik aus `_old`.

### `retroachievements.cfg`
Nicht in der offiziellen Custom-Datei-Liste, aber direkt an den `popup_h_offset`/`popup_v_offset`-Code
gekoppelt (wie schon beim letzten Merge entschieden). Der deutschsprachige Dokublock für beide Keys
wurde nach `popup_position` und vor odelots `debug=0`-Block eingefügt — an derselben Stelle wie zuvor in
`_old`. Keine odelot-Neuerungen in dieser Datei in diesem Release, daher kein Kollisionsrisiko.

### `README.md`
**Nicht wie im Auftrag pauschal "meine Version behalten" — siehe Anti-Trap-Fall in Abschnitt 3.**
`MainMister_new/README.md` = `MainMister_odelot/README.md` unverändert. Begründung dort.

---

## 3. Anti-Trap-Fälle & Kollisionen

### ⚠️ Anti-Trap-Fall (Abweichung von der wörtlichen Anweisung "README.md: meine Version behalten")

Der komplette Diff zwischen `MainMister_old/README.md` und `MainMister_odelot/README.md` (siehe
Abschnitt 1) besteht **ausschließlich** aus der Core-Support-Tabelle, und dort wiederum ausschließlich
aus der Gameboy/GBC-Zeile plus einer reinen Wording-Änderung, die odelot selbst vorgenommen hat. Es gibt
**keinen einzigen anderen inhaltlichen Unterschied** im gesamten 433-Zeilen-Dokument (auch die
Dokumentation der Custom-Config-Keys wie `show_challenge_show_popup`, `popup_position` etc. ist in
beiden Bäumen zeichengleich).

Die einzige "meine Version" von README, die es gäbe, ist exakt der Stand, den ein früherer Merge-Lauf
erzeugt hat, als GBC/Gameboy noch auf `hardcore_protected = 0` stand und die README-Tabelle passend dazu
auf "🔧 wired, in validation" zurückgestuft wurde. Dieser Zustand ist erwiesenermaßen bereits überholt:
`achievements_gameboy.cpp` steht in `MainMister_old` schon auf `hardcore_protected = 1` (Abschnitt 1),
und `MainMister_odelot`s README stuft Gameboy/GBC in genau diesem Release offiziell auf "✅ Supported"
hoch — beide Quellen bestätigen sich gegenseitig. Ein "meine Version" von README beizubehalten würde
hier exakt den im Auftrag beschriebenen Bug wiederholen (veralteter Wert fälschlich als Custom-Patch
gewertet) und zusätzlich einen inhaltlichen Widerspruch zwischen Code (`hardcore_protected = 1`) und
Doku (Tabelle sagt "nur wired, in validation") erzeugen.

**Entscheidung:** `README.md` = `_odelot` 1:1, nicht `_old`. Da es sich um eine reine Doku-Datei mit
minimalem Diff handelt, ist das risikolos reversibel, falls das nicht gewünscht ist — der komplette
Old-Diff steht oben in Abschnitt 1.

### Kollision: `menu.cpp` — odelot UART-Menü vs. mein `InfoAligned`-Patch
- **Odelot** erweiterte das UART-Auswahlmenü um UDP/SNI-Einträge (Zeilen ~258 und ~3882-3905).
- **Ich** erweiterte `InfoAligned()` um `h_offset`/`v_offset` (Zeilen ~8223-8242 in `_odelot`-Zählung).
- **Auflösung:** kombiniert, keine Überschneidung — unterschiedliche Funktionen, unterschiedliche
  Zeilenbereiche. Beide Änderungen vollständig erhalten (siehe Abschnitt 2).

### Kollision: `RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED` in `achievements.cpp`
Wie beim letzten Merge dokumentiert: odelots `gba_dump_trigger(...)` / `seladdr_trigdump_report(...)`
und meine komplette Popup-Formatierung treffen an derselben Stelle zusammen. In diesem Release hat sich
daran nichts geändert — beide Calls stehen weiterhin unverändert als erste beiden Statements im Block,
gefolgt von meiner Popup-Logik. Kein neuer Konflikt, keine Änderung zur letzten Auflösung nötig.

Keine weiteren Kollisionen gefunden — `scaler.cpp`, `retroachievements.cfg`, `achievements_gameboy.cpp`,
`support/snes/*` lagen an Stellen, die odelot in diesem Release nicht angefasst hat.

---

## 4. Übersprungen

- **Nichts übersprungen.** Insbesondere wurde der `scaler.cpp`-Patch **nicht** übersprungen: odelot hat
  die vier Bugs (Duplicate-Default-Argument, undeklariertes `limit`, `errno`-Namenskollision ×2) in
  diesem Release nicht gefixt — geprüft am tatsächlichen Code, siehe Abschnitt 2.
- Der im Auftrag erwähnte QMTech/EDID-Patch existiert weiterhin nicht in `MainMister_old` (schon beim
  letzten Merge festgestellt) — nichts zu reapplyen, nichts erfunden. `video.cpp` = `_odelot` 1:1
  (inklusive odelots eigener neuer EDID-Raw-Mfg-ID-Fallback-Logik, siehe Abschnitt 1).

---

## 5. Verifikation

- `diff -rq` `MainMister_odelot` vs. `MainMister_new` (ohne `bin/`, `releases/`): genau **5** abweichende
  Dateien — exakt die Custom-Dateien `achievements.cpp`, `scaler.cpp`, `menu.cpp`, `menu.h`,
  `retroachievements.cfg`. `README.md`, `achievements_gameboy.cpp`, `support/snes/*`, `video.cpp`,
  `user_io.cpp` sind absichtlich **byte-identisch** zu `_odelot` (siehe Abschnitt 1/3).
- `diff -rq` `MainMister_old` vs. `MainMister_new` (ohne `bin/`, `releases/`): genau **4** abweichende
  Quelldateien — `README.md` (Abschnitt 3), `menu.cpp` (odelot-UART-Feature reinkombiniert),
  `user_io.cpp`, `video.cpp` (beides reine odelot-Neuerungen, Abschnitt 1). `achievements.cpp`,
  `scaler.cpp`, `menu.h`, `retroachievements.cfg` sind byte-identisch zu `_old` — nichts Custom-Relevantes
  verloren. (`MERGE_REPORT.md` und `Main_Mister-v1.12.1.zip` existieren nur in `_old` als Altlasten der
  vorigen Merge-Runde und wurden bewusst nicht mitkopiert.)
- **`support/snes/snes.cpp` / `snes.h`**: `diff -q` gegen `_odelot` zeigt keine Abweichung — bestätigt
  byte-identisch. Der SNAC/Joystick-Totcode (`snes_get_joystick`, `last_joy1`/`last_joy2`, `case 0x37`)
  wurde **nicht** wieder eingebaut.
- **QMTech/EDID**: `grep -ril qmtech` über den kompletten `MainMister_new`-Baum liefert **keine
  Treffer**; `qmtech_edid_debug.h` existiert nicht.
- Klammer-/Klammernpaar-Balance (`{}`/`()`) in `achievements.cpp`, `scaler.cpp`, `menu.cpp`, `menu.h`,
  `retroachievements.cfg` geprüft — jeweils exakt ausgeglichen.
- **Kein Cross-Compile möglich** in dieser Windows-Umgebung (kein `arm-none-linux-gnueabihf-gcc`
  verfügbar) — rein manuelle/diff-basierte Verifikation, wie angewiesen wurde **nicht gebaut und nicht
  deployed**.
