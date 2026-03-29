#ifndef DEBUG_SERVER_H
#define DEBUG_SERVER_H

/* Startet den Debug-WebSocket-Server auf dem angegebenen Port.
   Gibt 0 bei Erfolg zurueck, -1 bei Fehler. */
int debug_server_start(int port);

/* Pollt fuer neue Connections und eingehende Nachrichten.
   Muss einmal pro Frame aufgerufen werden (non-blocking). */
void debug_server_poll(void);

/* Stoppt den Server und schliesst alle Connections. */
void debug_server_stop(void);

#endif
