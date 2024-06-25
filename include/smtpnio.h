#ifndef __SMTPNIO_H__
#define __SMTPNIO_H__

#include "selector.h"

void smtp_passive_accept(struct selector_key* key);

int get_historic_users();

int get_current_users();

int get_current_bytes();

int get_current_mails();

bool get_current_status();

void set_new_status(bool new_status);

#endif
