#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>
#include <time.h>
#include <linux/mxc_sim_interface.h>

#define SIM_DEV "/dev/mxc_sim"



typedef struct f_mode_tag {
	uint16_t f;
	int fmax;
} f_mode_t;

/* ISO7816-3 Table 7 - Fi and f(max) */
f_mode_t f_mode[] = {
	{  372,  4000000 },	/* 0000 */
	{  372,  5000000 },	/* 0001 */
	{  558,  6000000 },	/* 0010 */
	{  744,  8000000 },	/* 0011 */
	{ 1116, 12000000 },	/* 0100 */
	{ 1488, 16000000 },	/* 0101 */
	{ 1860, 20000000 },	/* 0110 */
	{   -1,       -1 },	/* 0111 RFU */
	{   -1,       -1 },	/* 1000 RFU */
	{  512,  5000000 },	/* 1001 */
	{  768,  7500000 },	/* 1010 */
	{ 1024, 10000000 },	/* 1011 */
	{ 1536, 15000000 },	/* 1100 */
	{ 2048, 20000000 },	/* 1101 */
	{   -1,       -1 },	/* 1110 RFU */
	{   -1,       -1 },	/* 1111 RFU */
};


typedef struct d_mode_tag {
	uint8_t d;
} d_mode_t;

/* ISO7816-3 Table 8 - Di */
d_mode_t d_mode[] = {
	{ -1 },	/* 0000 RFU */
	{  1 },	/* 0001 */
	{  2 },	/* 0010 */
	{  4 },	/* 0011 */
	{  8 },	/* 0100 */
	{ 16 },	/* 0101 */
	{ 32 },	/* 0110 */
	{ 64 },	/* 0111 */
	{ 12 }, /* 1000 */
	{ 20 }, /* 1001 */
	{ -1 }, /* 1010 RFU */
	{ -1 }, /* 1011 RFU */
	{ -1 }, /* 1100 RFU */
	{ -1 }, /* 1101 RFU */
	{ -1 }, /* 1110 RFU */
	{ -1 }, /* 1111 RFU */
};


void dump(uint8_t *buf, int len) {
	int i;
	
	for(i=0; i < len; i++) {
		printf("%02X ", buf[i]);
		if((i + 1) % 16 == 0) printf("\n");
	};
	printf("\n");
};


int _send(int sim_fd, uint8_t *tx, int len) {
	
	sim_xmt_t xmt_data;
	int ret;
	
	xmt_data.xmt_buffer = tx;
	xmt_data.xmt_length = len;

	ret = ioctl(sim_fd, SIM_IOCTL_XMT, &xmt_data);
	if (ret < 0)
		return ret;

	return 0;	
};

int _recv(int sim_fd, uint8_t *rx, int len) {
	
	sim_rcv_t rcv_data;
	int ret;
	
	rcv_data.rcv_buffer = rx;
	rcv_data.rcv_length = len;

	ret = ioctl(sim_fd, SIM_IOCTL_RCV, &rcv_data);
	if (ret < 0)
		return ret;
		
		
	return rcv_data.rcv_length;
};


/* Read fi, di and fmax from f1 (or pps1) encoding */
int ta1_to_fd(uint8_t ta1, uint16_t *f, uint8_t *d, int* fmax) {
	
	uint8_t ta1_h, ta1_l;
	ta1_h = ta1 >> 4;
	ta1_l = ta1 & 0x0F;
	
	if(f_mode[ta1_h].f == -1) {
		fprintf(stderr, "TA1/PPS1 has unknown fi (RFU)\n");
		return -1;
	};
		
	if(d_mode[ta1_l].d == -1) {
		fprintf(stderr, "TA1/PPS1 has unknown di (RFU)\n");
		return -1;
	};
	
	*f = f_mode[ta1_h].f;
	*fmax = f_mode[ta1_h].f;
	*d = d_mode[ta1_l].d;
	
	return 0;
};

/* Encode fi, di and fmax into pps1 (or f1) format */
/* Will match fi-modes with sclk frequency >= fmax */
int fd_to_pps1(int f, int d, int fmax) {
	
	int pps1 = 0;
	int i;
	
	if((f < 0) || (d < 0) || (fmax < 0)) return -1;
	
	for(i = 0; i <= 15; i++) {
		if(f_mode[i].f == f) {
			if(fmax <= f_mode[i].fmax) {
				pps1 = i << 4;
				break;
			};
		};
	};
	
	if(i == 16) {
		fprintf(stderr, "Could not find fi encoding for %d @ %.1f MHz\n", 
			f, (float)fmax / 1000000.0);
		
		return -1;
	};
		
	for(i = 0; i <= 15; i++) {
		if(d_mode[i].d == d) {
			pps1 |= i;
			break;
		};
	};
	
	if(i == 16) {
		fprintf(stderr, "Could not find di encoding for %d\n", d);
		return -1;
	};
		
	return pps1;	
};

/* Issue PPS to the card */
int pps(int sim_fd, int pps1) {
	
	int ret;
	uint8_t tx_buffer[4], rx_buffer[4];
	
	tx_buffer[0] = 0xff; /* ppss */
	tx_buffer[1] = 0x10; /* pps0 */
	tx_buffer[2] = pps1; 
	tx_buffer[3] = /* pck */
		tx_buffer[0] ^
		tx_buffer[1] ^
		tx_buffer[2];
		
	ret = _send(sim_fd, tx_buffer, 4);
	if(ret != 0) return -1;
	
	ret = _recv(sim_fd, rx_buffer, 4);	
	if(ret != 4) return -1;

	ret = memcmp(tx_buffer, tx_buffer, 4);
	if(ret != 0) {
		fprintf(stderr, "PPS: no response\n");
		return ret;
	};
	
	return 0;
};

/* Exchange APDU C-RP */
/* returns number of bytes received or -1 on error */
int apdu(int sim_fd, uint8_t *tx, int tx_len, uint8_t *rx, int rx_len) {
	
	int ret;
	uint8_t sw[2];
	uint8_t rx_misc[4];
	
	if(tx_len < 5) {
		fprintf(stderr, "APDU: command+data must be at least 5 bytes\n");
		return -1;
	};
	
	if(tx_len > 5 && rx_len != 0) {
		fprintf(stderr, "APDU: Bi-directional APDU data not supported\n");
		return -1;
	};
		
		
	
	ret = _send(sim_fd, tx, 5);
	if(ret != 0) {
		fprintf(stderr, "APDU: Failed to send command (%d bytes)\n", 5);
		return -1;
	};
	
	/* ISO7816-3 10.3.3 Procedure bytes */	
	/* TODO: this should be a state machine */
	
	do {
		ret = _recv(sim_fd, rx_misc, 1);
		if(ret != 1) {
			fprintf(stderr, "APDU: Did not receive procedure byte\n");
			return -1;
		};
	} while(rx_misc[0] == 0x60); /* 'NULL' */
	
	if(rx_misc[0] == tx[1]) { /* 'ACK' */

		if(tx_len > 5) {
			ret = _send(sim_fd, tx+5, tx_len - 5);
			if(ret != 0) {
				fprintf(stderr, "APDU: Failed to send data (%d bytes)\n", tx_len - 5);
				return -1;
			};
		} else if(rx_len > 0) {
			ret = _recv(sim_fd, rx, rx_len);
			if(ret < 0) {
				fprintf(stderr, "APDU: Error receiving data\n");
				return -1;
			};
			rx_len = ret;
		};

		ret = _recv(sim_fd, sw, 2);
		if(ret < 0) {
			fprintf(stderr, "APDU: Error receiving SW\n");
			return -1;
		};

	} else if((rx_misc[0] & 0xF0) == 0x60 || (rx_misc[0] & 0xF0) == 0x90) { /* SW1 */
		
		if(rx_len > 0 || tx_len > 0) {
			fprintf(stderr, "APDU: unexpected SW1 before data\n");
			return -1;
		};
		
		sw[0] = rx_misc[0];
		ret = _recv(sim_fd, sw+1, 1);
		if(ret < 0) {
			fprintf(stderr, "APDU: Error receiving SW2\n");
			return -1;
		};

		rx_len = 0;

	} else {
		fprintf(stderr, "APDU: Procedure byte invalid or not supported\n");
		return -1;
	};
	
	return rx_len;
};
	
	
int read_imsi(int sim_fd, uint8_t *imsi) {
	
	uint8_t apdu_select_mf[]   = { 0xA0, 0xA4, 0x00, 0x00, 0x02, 0x3F, 0x00 };
	uint8_t apdu_select_df[]   = { 0xA0, 0xA4, 0x00, 0x00, 0x02, 0x7F, 0x20 };
	uint8_t apdu_select_ef[]   = { 0xA0, 0xA4, 0x00, 0x00, 0x02, 0x6F, 0x07 };
	uint8_t apdu_read_binary[] = { 0xA0, 0xB0, 0x00, 0x00, 0x09 };

	uint8_t rx[32];
	int ret;

	
	ret = apdu(sim_fd, apdu_select_mf, sizeof(apdu_select_mf), 0, 0);
	if(ret < 0) {
		fprintf(stderr, "IMSI: SELECT MF failed\n");
		return -1;
	};
	
	ret = apdu(sim_fd, apdu_select_df, sizeof(apdu_select_df), 0, 0);
	if(ret < 0) {
		fprintf(stderr, "IMSI: SELECT DF_GSM failed\n");
		return -1;
	};

	ret = apdu(sim_fd, apdu_select_ef, sizeof(apdu_select_ef), 0, 0);
	if(ret < 0) {
		fprintf(stderr, "IMSI: SELECT EF_IMSI failed\n");
		return -1;
	};
		
	ret = apdu(sim_fd, apdu_read_binary, sizeof(apdu_read_binary), rx, 9);
	if(ret <= 0) {
		fprintf(stderr, "IMSI: READ_BINARY failed\n");
		return -1;
	};
	
	dump(rx, ret);
	
	return 0;

};


void usage() {
	fprintf(stderr,
		"USAGE: sim_test imsi [f d [sclk]] - read IMSI at specified f/d\n"
		"       sim_test imsi auto - read IMSI at max. f/d supported by the card\n"
		"       sim_test baud f d sclk - generate test pattern for etu measurement at f/d\n"
		"Default 372/1 @ 4 MHz\n"
		);
};

int main(int argc, char *argv[]) {

	int sim_fd;
	sim_timing_t timing_data;
	unsigned int protocol;
	sim_baud_t baud;
	sim_atr_t atr_data;
	int ret;
	uint16_t fi;
	uint8_t di;
	int fmax, fd_auto = 0;
	int ta1, pps1;
	uint8_t imsi[32];
	
	
	#define MODE_IMSI (1)
	#define MODE_BAUD (2)
	int mode;
	
	uint8_t tx[64];
	uint8_t rx[64];
	
	/* parse commandline args */
	
	if(argc < 2) {
		usage();
		return 0;
	};
	
	fi = 372;
	di = 1;
	fmax = 4000000;
	fd_auto = 0;
	
	if(strcmp(argv[1], "imsi") == 0) {
		
		mode = MODE_IMSI;
		
		if(argc == 4 || argc == 5) {
			fi = atoi(argv[2]);
			di = atoi(argv[3]);
			if(argc == 5) {
				fmax = atoi(argv[4]);
			};
		} else if(argc == 3) {
			if(strcmp(argv[2], "auto") == 0) {
				fd_auto = 1;
			} else {
				usage();
				return -EINVAL;				
			};		
		} else if(argc == 2) {
			fprintf(stderr, "Using default baud rate 372/1\n");
		} else {
			usage();
			return -EINVAL;			
		};
		
	} else if(strcmp(argv[1], "baud") == 0) {

		mode = MODE_BAUD;

		if(argc == 5) {
			fi = atoi(argv[2]);
			di = atoi(argv[3]);
			fmax = atoi(argv[4]);
		} else {
			usage();
			return -EINVAL;
		};
		
	} else {
		usage();
		return -EINVAL;
	};
	
	if(fi == 0 || di == 0 || fmax == 0) {
		usage();
		return -EINVAL;
	};
	
	/* open the SIM and configure timings */
		
	sim_fd = open(SIM_DEV, O_RDWR);
	if(sim_fd < 0) {
		fprintf(stderr, "Could not open " SIM_DEV ": %d\n", sim_fd);
		return sim_fd;
	};
	
	/* SCLK must be set before COLD_RESET */
	/* TODO: change SCLK after ATR and cold-reset again */
	ioctl(sim_fd, SIM_IOCTL_SET_SCLK, &fmax);
	
	ioctl(sim_fd, SIM_IOCTL_COLD_RESET);
	
	protocol = SIM_PROTOCOL_T0;
	ret = ioctl(sim_fd, SIM_IOCTL_SET_PROTOCOL, &protocol);
	
	timing_data.wwt = 9600;	/* RX: wait time between chars, in sclk cycles. Overrides cwt & bwt if not zero */
	timing_data.cwt = 0;	/* max. ETUs between bytes received */
	timing_data.cgt = 0;	/* ETUs inserted between consecutive bytes sent ('stop bits') */
	timing_data.bgt = 0;	/* min. ETUs between last byte sent and first byte received */
	timing_data.bwt = 0;	/* max. ETUs between last byte sent and first byte received */

	/* TODO: calculate wwt every baud_rate change as in ISO7816-3 10.2 WT = WI * 960 * fi/f */

	ret = ioctl(sim_fd, SIM_IOCTL_SET_TIMING, &timing_data);
	
	if(mode == MODE_IMSI) {
		
		/* re-write default baud settings so driver does not complain during pps */
		
		baud.fi = 372;
		baud.di = 1;

		ioctl(sim_fd, SIM_IOCTL_SET_BAUD, &baud);
		
		
		
		atr_data.atr_buffer = rx;
		ret = ioctl(sim_fd, SIM_IOCTL_GET_ATR, &atr_data);
		if (ret < 0) {
			fprintf(stderr, "Could not read ATR: %d\n", ret);
			return -ECOMM;
		};

		
		
		/* get ta1 from atr */

		ta1 = -1;

		if(atr_data.size == 0) {
			fprintf(stderr, "Warning: empty ATR\n");
		} else if(atr_data.size >= 2) {
			if(rx[0] == 0x3b) {
				fprintf(stderr, "ATR: direct convention (TS = 0x3B)\n");
			} else if(rx[0] == 0x3f) {
				fprintf(stderr, "ATR: inverse convention (TS = 0x3F)\n");
			} else {
				fprintf(stderr, "ATR: invalid initial byte (TS = 0x%02X)\n", rx[0]);
				return -EINVAL;
			};
			
			if(atr_data.size >= 3) {
				if(rx[1] & (1<<4))
					ta1 = rx[2];
			};
		};


		/* determine f/d and fmax */

		if(ta1 != -1) {
			if(fd_auto) {

				ret = ta1_to_fd(ta1, &fi, &di, &fmax);
				if(ret < 0) {
					fprintf(stderr, "Invalid TA1: %02X\n", ta1);
					return -EINVAL;
				};

				fprintf(stderr, "PPS: Using max. baud rate: %d/%d, %.1f MHz\n", fi, di, (float)fmax / 1000000.0);

				pps1 = ta1;
			} else {
				fprintf(stderr, "PPS: Using requested baud rate: %d/%d, %.1f MHz\n", fi, di, (float)fmax / 1000000.0);
				
				pps1 = fd_to_pps1(fi, di, fmax);			
			};

			ret = pps(sim_fd, pps1);
			if(ret == 0) {
				fprintf(stderr, "PPS: ok\n");
			} else {
				fprintf(stderr, "PPS: failed\n");
				return -ECOMM;
			};
			
			baud.fi = fi;
			baud.di = di;
			
			ioctl(sim_fd, SIM_IOCTL_SET_BAUD, &baud);


		} else {
			fprintf(stderr, "Card does not support PPS, continuing at 372/1\n");
		};		
		
		ret = read_imsi(sim_fd, imsi);
		if(ret != 0) {
			fprintf(stderr, "Failed to read IMSI\n");
			
		};

	} else if(mode == MODE_BAUD) {
		
		baud.fi = fi;
		baud.di = di;
		
		ioctl(sim_fd, SIM_IOCTL_SET_BAUD, &baud);

		/* Send test pattern for ETU measurement */
		printf("Test pattern (0xaaaaaaaa)\n");
		for(int i=0; i<100; i++) {
			tx[0] = 0xaa;
			tx[1] = 0xaa;
			tx[2] = 0xaa;
			tx[3] = 0xaa;
			_send(sim_fd, tx, 4);

			usleep(10000);
		};
		printf("\n");
	};
	
	close(sim_fd);
	
	return 0;
};
