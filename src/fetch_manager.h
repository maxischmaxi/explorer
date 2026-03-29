#ifndef FETCH_MANAGER_H
#define FETCH_MANAGER_H

#include "resource_fetch.h"

/* Initialisiert den parallelen Fetch-Manager (curl_multi). */
void fetch_manager_init(void);

/* Sortiert Queue nach Prioritaet und startet Fetching. */
void fetch_manager_start(PendingQueue *queue);

/* Non-blocking Poll: startet neue Transfers, sammelt fertige ein.
   Fertige Ressourcen werden in col eingefuegt.
   Gibt Anzahl verbleibender Transfers zurueck (laufend + Queue). */
int fetch_manager_poll(ResourceCollection *col);

/* Bricht alle laufenden Transfers ab. */
void fetch_manager_abort(void);

void fetch_manager_cleanup(void);

#endif
