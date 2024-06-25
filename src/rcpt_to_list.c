#include "rcpt_to_list.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

struct rcpt_node*
create_rcpt_node(const char* email)
{
	struct rcpt_node* new_node = (struct rcpt_node*)malloc(sizeof(struct rcpt_node));
	if (new_node == NULL) {
		perror("Failed to allocate memory for new recipient node");
		exit(EXIT_FAILURE);
	}
	strncpy(new_node->email, email, MAX_EMAIL_LENGTH);
	new_node->email[MAX_EMAIL_LENGTH - 1] = '\0';  // Ensure null-termination
	new_node->next = NULL;
	return new_node;
}

void
add_rcpt_to_list(struct rcpt_node** head, const char* email)
{
	struct rcpt_node* new_node = create_rcpt_node(email);
	new_node->next = *head;
	*head = new_node;
}

void
free_rcpt_list(struct rcpt_node* head)
{
	struct rcpt_node* current = head;
	struct rcpt_node* next;

	while (current != NULL) {
		next = current->next;
		free(current);
		current = next;
	}
}

void
close_fds(struct rcpt_node* head)
{
	struct rcpt_node* current = head;
	while (current != NULL) {
		close(current->file_fd);

		char file_path[MAX_EMAIL_LENGTH * 3];
		snprintf(file_path, sizeof(file_path), "mails/%s/tmp/%s", current->email, current->filename);

		struct stat st;
		if (stat(file_path, &st) == -1) {
			fprintf(stderr, "Error getting file status for %s\n", file_path);
			abort();
		}

		char file_path_new[400];
		snprintf(file_path_new, sizeof(file_path_new), "mails/%s/new/%s", current->email, current->filename);

		if (rename(file_path, file_path_new) == -1) {
			fprintf(stderr, "Error renaming file %s to %s\n", file_path, file_path_new);
			abort();
		}

		current = current->next;
	}
}

void
write_to_files(struct rcpt_node* head, struct data_parser* p)
{
	struct rcpt_node* current = head;
	while (current != NULL) {
		// TODO ????
		int n = write(current->file_fd, p->data_buffer.data, p->data_buffer.write - p->data_buffer.data);
		current = current->next;
	}
}

void
create_uuid(char* uuid_str)
{
	uuid_t uuid;
	uuid_generate(uuid);
	uuid_unparse(uuid, uuid_str);
}

void
create_mails_files(struct rcpt_node* head, char* mailfrom, char* program, bool transformations)
{
	struct stat st;
	if (stat("mails", &st) == -1) {
		if (mkdir("mails", 0777) == -1) {
			fprintf(stderr, "Error creating directory mails\n");
			abort();
		}
	}

	struct rcpt_node* current = head;
	while (current != NULL) {
		char dir_name[6 + MAX_EMAIL_LENGTH];
		snprintf(dir_name, sizeof(dir_name), "mails/%s", current->email);

		struct stat st;
		if (stat(dir_name, &st) == -1) {
			if (mkdir(dir_name, 0777) == -1) {
				fprintf(stderr, "Error creating directory %s\n", dir_name);
				abort();
			}
		}

		char dir_name_new[sizeof(dir_name) + 4];
		snprintf(dir_name_new, sizeof(dir_name_new), "%s/new", dir_name);
		if (stat(dir_name_new, &st) == -1) {
			if (mkdir(dir_name_new, 0777) == -1) {
				fprintf(stderr, "Error creating directory %s\n", dir_name);
				abort();
			}
		}

		char dir_name_cur[sizeof(dir_name) + 4];
		snprintf(dir_name_cur, sizeof(dir_name_cur), "%s/cur", dir_name);
		if (stat(dir_name_cur, &st) == -1) {
			if (mkdir(dir_name_cur, 0777) == -1) {
				fprintf(stderr, "Error creating directory %s\n", dir_name);
				abort();
			}
		}

		char dir_name_tmp[sizeof(dir_name) + 4];
		snprintf(dir_name_tmp, sizeof(dir_name_tmp), "%s/tmp", dir_name);
		if (stat(dir_name_tmp, &st) == -1) {
			if (mkdir(dir_name_tmp, 0777) == -1) {
				fprintf(stderr, "Error creating directory %s\n", dir_name);
				abort();
			}
		}

		char uuid_str[37];
		create_uuid(uuid_str);
		snprintf(current->filename, sizeof(current->filename), "%s.txt", uuid_str);

		char file_path[400];
		snprintf(file_path, sizeof(file_path), "%s/%s", dir_name_tmp, current->filename);

		int fd = open(file_path, O_CREAT | O_WRONLY, 0777);
		if (fd == -1) {
			fprintf(stderr, "Error creating file %s\n", file_path);
			abort();
		}

		time_t now = time(NULL);
		struct tm* t = localtime(&now);

		char time_str[100];
		strftime(time_str, sizeof(time_str), "%a %b %d %H:%M:%S %Y", t);
		char from_header[512];
		snprintf(from_header, sizeof(from_header), "From %s  %s\n", mailfrom, time_str);
		write(fd, from_header, strlen(from_header));

		current->file_fd = fd;

		if (transformations) {
			int fds[2];
			if (pipe(fds) == -1) {
				fprintf(stderr, "Error creating pipe\n");
				abort();
			}

			pid_t pid = fork();
			if (pid == -1) {
				fprintf(stderr, "Error forking\n");
				abort();
			}

			if (pid == 0) {
				close(STDIN_FILENO);
				dup(fds[0]);  // read end of where app writes
				close(STDOUT_FILENO);
				dup(fd);  // write end of where app reads

				close(fds[0]);
				close(fds[1]);
				close(fd);

				char* args[] = { program, NULL };
				execvp(program, args);
				fprintf(stderr, "Error executing program %s\n", program);
				abort();
			}

			close(fds[0]);
			close(fd);
			current->file_fd = fds[1];
		}

		current = current->next;
	}
}
