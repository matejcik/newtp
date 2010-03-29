#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

extern int sock_client;

#define DELIMITER '\n'
#define BUFSIZE 256 * 1024

int recv_command (char *buf, int len)
{
	char *origbuf = buf;
	while (1) {
		int read = recv(sock_client, buf, len, 0);
		if (read < 0) return -1; /* error occured */
		if (read == 0) return 0; /* command is not complete */
		len -= read;
		if (len <= 0) return 0; /* buffer is full */
		while (read--) {
			if (*buf == DELIMITER) return buf - origbuf;
			buf++;
		}
	}
}

int scan_number (const char* buf, int len, unsigned long *number)
{
	int num = 0;
	int i = 0;
	while (i < len) {
		if (buf[i] >= '0' && buf[i] <= '9') {
			num = num * 10 + buf[i] - '0';
		} else {
			*number = num;
			return i;
		}
		i++;
	}
	*number = 0;
	return 0;
}

char* scan_netstring (char* buf, int len, unsigned long *strl)
{
	unsigned long num;
	int i = scan_number(buf, len, &num);
	if (buf[i] != ':') // not a netstring
		return NULL;
	if (i > 1 && num == 0) // leading zeros
		return NULL;
	if (i > 20) // number too long
		return NULL;
	if (buf[i + 1 + num] != ',') // bad ending
		return NULL;
	buf[i + 1 + num] = 0;
	if (strl) *strl = num;
	return buf+i+1;
}

int send_full (const char *buf, int len)
{
	int origlen = len;
	while (1) {
		int wrote = send(sock_client, buf, len, 0);
		fprintf(stderr, "%*s", wrote, buf);
		if (wrote == len) return origlen;
		else if (wrote < 0) return -1;
		else if (wrote == 0) return 0;
		else {
			buf += wrote;
			len -= wrote;
		}
	}
}

int send_string (const char *string)
{
	return send_full(string, strlen(string));
}

int send_ns (const char *string)
{
	char buf[1024];
	int strl = strlen(string);
	int len = snprintf(buf, 1024, "%d:%*s,", strl, strl, string);
	return send_full(buf, len);
}

void kill_connection (const char* why)
{
	send_string(why);
	send_string("\n");
	close(sock_client);
	exit(1);
}

void send_stat (const struct stat* stats)
{
	char buf[256];
	char modes[3];
	int len, len2;

	modes[0] = S_ISDIR(stats->st_mode) ? 'd' : '-';

	modes[1] = (stats->st_mode & S_IRUSR) ? 'r' : '-';
	modes[2] = (stats->st_mode & S_IWUSR) ? 'w' : '-';

	len = snprintf(buf, 256, "%.3s %lu ", modes, stats->st_size);
	len2 = strftime(buf+len, 256-len, "%F %T ", gmtime(&stats->st_mtime));
	send_full(buf, len+len2);
}

int validate_path(const char* path)
{
	int after_delim = 1;
	if (*path++ != '/') return 0;
	while (*path) {
		if (*path != '.') after_delim = 0;
		if (*path == '.' && after_delim) after_delim = 2;
		if (*path == '/' && after_delim) return 0;
		if (*path == '/' && !after_delim) after_delim = 1;
		path++;
	}
	return after_delim != 2;
}

void do_list (char* buffer, int len)
{
	char* dir;
	char buf[1024];
	struct stat stats;
	DIR* dirlist;
	struct dirent* ent;
	int err, count = 0;
	unsigned long dirlen;

	fprintf(stderr, "child %d: this is LIST\n", getpid());

	buffer += 5, len -= 5;
	dir = scan_netstring(buffer, len, &dirlen);
	if (!dir) {
		kill_connection("you're talking garbage. go home.");
	}

	if (!validate_path(dir)) {
		kill_connection("bad path, go away");
	}

	// strip leading slash, work in cwd
	dir++; dirlen--;
	// strip trailing slash
	if (dirlen > 0 && dir[dirlen - 1] == '/') dir[--dirlen] = 0;

	if (! *dir) {
		dir = ".";
		dirlen = 1;
	}

	strncpy(buf, dir, dirlen);
	buf[dirlen] = '/';

	err = stat(dir, &stats);
	if (err) {
		send_string("ERR\n");
	} else {
		send_string("OK\n");
		send_stat(&stats);
		if (S_ISDIR(stats.st_mode)) {
			send_full("1:.,\n", 5);
			count++;
			dirlist = opendir(dir);
			while ((ent = readdir(dirlist))) {
				if ((strcmp(ent->d_name, ".") == 0) || strcmp(ent->d_name, "..") == 0) continue;
				strcpy(buf + dirlen + 1, ent->d_name);
				err = stat(buf, &stats);
				if (!err) {
					send_stat(&stats);
					send_ns(ent->d_name);
					send_full("\n", 1);
					count++;
				} else {
					fprintf(stderr, "error: %d %s\n", errno, strerror(errno));
				}
			}
		} else {
			send_ns(basename(dir));
			send_full("\n", 1);
		}
		len = snprintf(buf, 1024, "OK %d\n", count);
		send_full(buf, len);
	}
}

void do_read(char* buffer, int len)
{
	unsigned long offset, limit;
	int whole = 0;
	int nlen, fd;
	char* filename;
	struct stat stats;
	char* buf;

	fprintf(stderr, "child %d: this is READ\n", getpid());

	buffer += 5, len -= 5;

	nlen = scan_number(buffer, len, &offset);
	if (buffer[nlen++] != ' ') kill_connection("you're talking garbage. go home.");
	buffer += nlen; len -= nlen;

	if (*buffer == '$') {
		whole = 1;
		nlen = 1;
	} else {
		nlen = scan_number(buffer, len, &limit);
	}

	if (buffer[nlen++] != ' ') kill_connection("you're talking garbage. go home.");
	buffer += nlen; len -= nlen;

	filename = scan_netstring(buffer, len, NULL);
	if (!filename) kill_connection("you're talking garbage. go home.");

	if (!validate_path(filename)) kill_connection("bad path, go away");
	filename++;

	fd = open(filename, O_RDONLY);
	if (fd < 1) {
		fprintf(stderr, "child %d: %s\n", getpid(), strerror(errno));
		send_string("ERR\n");
	} else {
		fstat(fd, &stats);
		if (!S_ISREG(stats.st_mode)) {
			send_string("ERR you can't read a directory\n");
			return;
		}
		if (whole) limit = stats.st_size;
		if (offset >= stats.st_size) {
			send_string("ERR\n");
			return;
		}
		if (limit > stats.st_size - offset) limit = stats.st_size - offset;

		buf = malloc ( BUFSIZE );
		nlen = sprintf(buf, "OK %ld:", limit);
		send_full(buf, nlen);

		lseek(fd, offset, SEEK_SET);
		nlen = 0;
		while (nlen < limit) {
			int rem = (limit - nlen < BUFSIZE) ? (limit - nlen) : BUFSIZE;
			int r = read(fd, buf, rem);
			if (r < 1) {// something went horribly wrong
				free(buf);
				kill_connection("uh-oh");
			}
			send_full(buf, r);
			nlen += r;
		}
		send_full(",\n", 2);
		free(buf);
	}
}

void do_write(const char* buffer, int len) { }

#define CMD_LIST "LIST"
#define CMD_READ "READ"
#define CMD_WRITE "WRITE"
void do_command (char * buffer, int len)
{
	if (strncmp(buffer, CMD_LIST, strlen(CMD_LIST)) == 0) {
		do_list(buffer, len);
	} else if (strncmp(buffer, CMD_READ, strlen(CMD_READ)) == 0) {
		do_read(buffer, len);
	} else if (strncmp(buffer, CMD_WRITE, strlen(CMD_WRITE)) == 0) {
		do_write(buffer, len);
	}
}

#define CMD_LEN 16384
#define GREETING "welcome back to Citadel Station!\n"
void work (void)
{
	char buf[CMD_LEN + 1];
	buf[CMD_LEN] = 0;

	fprintf(stderr, "child %d for connection\n", getpid());
	//sleep(10);

	/* greeting */
//	send_string(GREETING);
	while (1) {
		int cmd = recv_command (buf, CMD_LEN);
		if (cmd < 0) {
			fprintf(stderr, "child %d: %s\n", getpid(), strerror(errno));
		} else if (cmd == 0) {
			kill_connection("what?");
			return;
		} else {
			fprintf(stderr, "child %d: command %*s", getpid(), cmd, buf);
			do_command (buf, cmd);
		}
	}
}
