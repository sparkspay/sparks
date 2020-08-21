
Debian
====================
This directory contains files used to package sparksd/sparks-qt
for Debian-based Linux systems. If you compile sparksd/sparks-qt yourself, there are some useful files here.

## sparks: URI support ##


sparks-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install sparks-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your sparks-qt binary to `/usr/bin`
and the `../../share/pixmaps/sparks128.png` to `/usr/share/pixmaps`

sparks-qt.protocol (KDE)

