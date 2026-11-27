Aldi Smart Display: native SD-card examples
Copy the contents of sd-card/ to the root of a FAT32 DATA microSD.
Requires the project's SD autostart firmware hook. The separate FEL card is
for backup/flashing, not these applications.

The supplied root autostart.sh selects Hello World by default.
Optional folders home-panel/ and mova-panel/ can coexist with hello-world/.
To select a panel, back up the root autostart.sh and replace it with the
corresponding source folder's autostart.sh; see the repository README.
Configure only the selected panel's private secrets.json using its example.
Never publish that private file. The MOVA example defaults to an offline demo
and portrait orientation, configurable in mova-panel/start.sh.

Sources: demo/, home-panel/ and mova-panel/ in the repository.
Application code: GPL-2.0-or-later; retain bundled dependency/font notices.
Stop the running application before replacing files or removing the card.
