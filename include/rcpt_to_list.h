#ifndef __LIST_H__
#define __LIST_H__

#include "data.h"

#define MAX_EMAIL_LENGTH 40

struct rcpt_node
{
	char email[MAX_EMAIL_LENGTH];
	struct rcpt_node* next;
	int file_fd;
	char filename[MAX_EMAIL_LENGTH + 5];  // 5 = strlen(".txt") + 1
};

struct rcpt_node* create_rcpt_node(const char* email);
void add_rcpt_to_list(struct rcpt_node** head, const char* email);
void free_rcpt_list(struct rcpt_node* head);
void close_fds(struct rcpt_node* head);
void write_to_files(struct rcpt_node* head, struct data_parser* p);
void create_mails_files(struct rcpt_node* head, char* mailfrom, char* program, bool transformations);

#endif
