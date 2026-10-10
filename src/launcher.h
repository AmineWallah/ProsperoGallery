#ifndef LAUNCHER_H
#define LAUNCHER_H

#define LAUNCHER_TITLE_ID "PRGL00001"

// Adds (or updates) a "ProsperoGallery" icon in the PS5's Media tab. Opening it
// launches the console's web browser on http://127.0.0.1:<port>/connect,
// which shows the QR code and the URL for phones.
// Returns 1 if it installed or updated the icon, 0 if it was already
// up to date (or on PC), -1 on error.
int launcher_install(int port);

#endif
