// SPDX-FileCopyrightText: 2022 iyzsong
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>
#include <linux/uinput.h>
#include <poll.h>
#include <alsa/asoundlib.h>


// uinput fd
static int ui;

// MIDI note numbers are 0 to 127.
static int keymap[128] = {
//      C  C# D  D# E  F  F# G  G# A  A# B
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L,
	KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X,
	KEY_Y, KEY_Z, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0
};

static void uinput_emit(int type, int code, int val)
{
	struct input_event ie;
	ie.type = type;
	ie.code = code;
	ie.value = val;
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;
	write(ui, &ie, sizeof(ie));
}

static void key_pressed(int key)
{
	uinput_emit(EV_KEY, key, 1);
	uinput_emit(EV_SYN, SYN_REPORT, 0);
}

static void key_released(int key)
{
	uinput_emit(EV_KEY, key, 0);
	uinput_emit(EV_SYN, SYN_REPORT, 0);
}

static void handle_event(const snd_seq_event_t *ev)
{
	if (ev->type != SND_SEQ_EVENT_NOTEON && ev->type != SND_SEQ_EVENT_NOTEOFF)
		return;
	int key = keymap[ev->data.note.note];
	if (key == 0)
		return;

	if (ev->type == SND_SEQ_EVENT_NOTEON) {
		if (ev->data.note.velocity)
			key_pressed(key);
		else
			key_released(key);
	}
	if (ev->type == SND_SEQ_EVENT_NOTEOFF) {
		key_released(key);
	}
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <PORT-NAME>\n", argv[0]);
		exit(1);
	}

	int err;
	struct uinput_setup usetup;
	ui = open("/dev/uinput", O_WRONLY|O_NONBLOCK);
	ioctl(ui, UI_SET_EVBIT, EV_KEY);
	for (int i = 0; i <= 127; ++i) {
		int key = keymap[i];
		if (key > 0)
			ioctl(ui, UI_SET_KEYBIT, key);
	}
	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor = 0x1234;
	usetup.id.product = 0x5678;
	strcpy(usetup.name, "MIDIOKBD");
	ioctl(ui, UI_DEV_SETUP, &usetup);
	ioctl(ui, UI_DEV_CREATE);

	static snd_seq_t *seq;
	static snd_seq_addr_t seq_addr;
	err = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
	if (err < 0) {
		fprintf(stderr, "snd_seq_open: %s\n", snd_strerror(err));
		exit(1);
	}
	err = snd_seq_set_client_name(seq, "midiokbd");
	if (err < 0) {
		fprintf(stderr, "snd_seq_set_client_name: %s\n", snd_strerror(err));
		exit(1);
	}

	const char *port = argv[1];
	err = snd_seq_parse_address(seq, &seq_addr, port);
	if (err < 0) {
		fprintf(stderr, "snd_seq_parse_address: %s\n", snd_strerror(err));
		exit(1);
	}
	err = snd_seq_create_simple_port(seq, "midiokbd",
			SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
			SND_SEQ_PORT_TYPE_MIDI_GENERIC|SND_SEQ_PORT_TYPE_APPLICATION);
	if (err < 0) {
		fprintf(stderr, "snd_seq_create_simple_port: %s\n", snd_strerror(err));
		exit(1);
	}
	err = snd_seq_connect_from(seq, 0, seq_addr.client, seq_addr.port);
	if (err < 0) {
		fprintf(stderr, "snd_seq_connect_from: %s\n", snd_strerror(err));
		exit(1);
	}
	int npfds = snd_seq_poll_descriptors_count(seq, POLLIN);
	struct pollfd *pfds;
	pfds = alloca(sizeof(*pfds) * npfds);
	while (1) {
		snd_seq_poll_descriptors(seq, pfds, npfds, POLLIN);
		if (poll(pfds, npfds, -1) < 0)
			break;
		do {
			snd_seq_event_t *event;
			err = snd_seq_event_input(seq, &event);
			if (err < 0)
				break;
			if (event)
				handle_event(event);
		} while (err > 0);
	}

	ioctl(ui, UI_DEV_DESTROY);
	close(ui);

	return 0;
}
