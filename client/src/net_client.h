// WiFi station side of the client: join the server's AP with a static IP and
// rejoin automatically after the link drops.
#ifndef SKISIM_NET_CLIENT_H
#define SKISIM_NET_CLIENT_H

#include <stdbool.h>

// Initialize the wireless chip and start the first join attempt.
// Returns 0 on success, nonzero if the chip could not be initialized.
int net_client_init(void);

// Call often from the main loop. Tracks the link and rejoins when needed.
void net_client_poll(void);

// True while associated with the AP and holding the static IP.
bool net_client_is_connected(void);

#endif // SKISIM_NET_CLIENT_H
