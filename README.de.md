# Aldi Smart Display – Kurzanleitung

Dieses inoffizielle Community-Projekt ermöglicht **ADB-Zugriff**, den
**automatischen Start eigener Programme von microSD** und eine kleine
**Hello-World-Demo**. Es ist kein offizielles Aldi-Projekt.

**Mit Datenkarte:** Die Firmware startet `autostart.sh` im Hauptverzeichnis.
**Ohne Datenkarte oder ohne dieses Skript:** Die Herstelleroberfläche läuft.

## Was du brauchst

- Das getestete Display mit Allwinner **V853**, **16 MiB SPI-NOR** und
  **1024 × 600** Pixeln. Andere Firmwareversionen werden vor Änderungen abgelehnt.
- Ein USB-Datenkabel zum Datenanschluss; bei unserem Gerät war das **Micro-USB**.
- Zwei microSD-Karten: eine **FEL-/Rettungskarte**, eine normale
  **FAT32-Datenkarte** für Programme.
- Windows x64 oder Ubuntu und Python.

## Ablauf

1. **Werkzeuge vorbereiten:** Folge der [Windows-Anleitung](docs/windows.md)
   oder [Ubuntu-Anleitung](docs/ubuntu.md). Die Befehle dort sind vollständig.
2. **FEL-Karte erstellen:** `tools/make_fel_card.py` erzeugt `build/fel-card.img`.
   Schreibe dieses Image ausschließlich auf die Rettungskarte. Ihr bisheriger
   Inhalt wird dadurch unzugänglich. Die zweite Karte bleibt eine FAT32-Datenkarte.
3. **FEL starten:** Display ausschalten, FEL-Karte einlegen, Powerknopf halten
   und über den Datenanschluss verbinden. Gesucht ist USB-ID **`1f3a:efe8`**.
   Unter Windows benötigt dieses Gerät gegebenenfalls WinUSB über Zadig.
4. **Eigenes Backup erstellen:** `tools/firmware.py backup` liest die kompletten
   16 MiB zweimal und vergleicht sie. Sichere `work/` zusätzlich außerhalb des
   Rechners. Dieses Verzeichnis nicht veröffentlichen.
5. **Firmware lokal vorbereiten:** `tools/firmware.py prepare` aktiviert ADB,
   korrigiert den USB-Gerätemodus und ergänzt den SD-Start. Der anschließende
   Befehl `tools/firmware.py install` zeigt zunächst nur den Plan an.
6. **Einspielen:** Erst `tools/firmware.py install --write` schreibt die beiden
   vorgesehenen Bereiche. Es folgen Kontrolllesen und Gesamtvergleich. Während
   dieser Minuten nicht ausschalten. Auf **`FULL_FLASH_VERIFIED`** warten.
7. **Datenkarte verwenden:** Inhalt von **`sd-card/`** auf die FAT32-Karte
   kopieren. Ausschalten, FEL-Karte gegen Datenkarte tauschen und normal starten.
   Nach ungefähr **25 Sekunden** sollte die Demo automatisch erscheinen.

Die genannten Python-Skripte werden mit dem Interpreter aus der virtuellen
Umgebung aufgerufen. Die Plattform-Anleitungen zeigen jeweils die genaue
Schreibweise; unter Windows zum Beispiel
`.\.venv\Scripts\python.exe tools/firmware.py backup`.

## Dateien auf der Datenkarte

```text
autostart.sh
hello-world/
  start.sh
  hello-world
```

Der Button **Back to menu** beendet die Demo bis zum nächsten Neustart.
Antippen des Hintergrunds wechselt die Farbe. Der Untertitel lautet
**access developed by Pfleiderer-Adrian**.

Neue Anwendungen können später durch Austauschen der SD-Dateien installiert
werden. Dafür ist normalerweise kein erneutes Flashen nötig. Erst das Programm
beenden, bevor du seine Dateien überschreibst oder die Karte herausnimmst.

## ADB prüfen

Unter Ubuntu: `adb devices -l`.
Unter Windows mit den separat entpackten Google Platform-Tools:
`.\platform-tools\adb.exe devices -l`.

ADB erscheint als **Tina ADB, `18d1:d002`**. Es ist ein anderes USB-Gerät als FEL
und hat unter Windows eine eigene Treiberzuordnung. Während des Starts kann USB
kurz verschwinden und nach der Korrektur automatisch zurückkehren.

## Was veröffentlicht wird

Quellcode, Startskripte, FEL-Stub, Demo und Anleitungen. **Keine komplette
Hersteller-Firmware und keine persönlichen Geräte-Backups.** Jeder erstellt
seine angepasste Firmware aus dem eigenen Backup. Die Firmware-Werkzeuge prüfen
die unterstützte Version und die Änderungen, bevor sie schreiben.

[Technische Hintergründe](docs/technical.md) · [Entwicklung](docs/development.md)
· [Wiederherstellung](docs/recovery.md) · [Englische Übersicht](README.md)
