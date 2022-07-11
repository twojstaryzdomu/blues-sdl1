
#ifndef MESSAGE_H__
#define MESSAGE_H__

#include <string.h>

#define MESSAGE_MAX 30
#define MESSAGE_TIMELIMIT 1000
#define MAX_MESSAGES 5

char	*_queue[MAX_MESSAGES];

struct message_t {
	void	(*add)(char *m, ...);
	char*	(*get)();
	void	(*clear)(const char *m);
	void	(*clear_all)();
};

static void add(char *m, ...) {
	int j;
	for (int i = 0; i < MAX_MESSAGES; i++)
		if (_queue[i] && strcmp(m, _queue[i]) == 0)
			return;
	for (int i = 0; i < MAX_MESSAGES; i++)
		if (!_queue[i])
			j = i;
	_queue[ 0 + j ] = m;
}

static char* get() {
	for (int i = 0; i < MAX_MESSAGES; i++)
		if (_queue[i])
			return _queue[i];
	return 0;
}

static void clear(const char *m) {
	for (int i = 0; i < MAX_MESSAGES; i++)
		if (_queue[i] && strcmp(m, _queue[i]) == 0)
			_queue[i] = 0;
}

static void clear_all() {
	for (int i = 0; i < MAX_MESSAGES; i++)
		_queue[i] = 0;
}

#endif
