// WiFi access point run by the server, with a static IP and no DHCP server.
#ifndef SKISIM_NET_AP_H
#define SKISIM_NET_AP_H

// Initialize the wireless chip and start the AP.
// Returns 0 on success, nonzero if the chip could not be initialized.
int net_ap_init(void);

// Number of stations (clients) currently associated with the AP.
int net_ap_station_count(void);

#endif // SKISIM_NET_AP_H
