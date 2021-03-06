/*	$OpenBSD: sff.c,v 1.1 2019/04/10 10:14:37 dlg Exp $ */

/*
 * Copyright (c) 2019 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SMALL

#include <sys/ioctl.h>

#include <net/if.h>

#include <math.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <vis.h>

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

#ifndef ISSET
#define ISSET(_w, _m)	((_w) & (_m))
#endif

#define SFF8024_ID_UNKNOWN	0x00
#define SFF8024_ID_GBIC		0x01
#define SFF8024_ID_MOBO		0x02 /* Module/connector soldered to mobo */
				     /* using SFF-8472 */
#define SFF8024_ID_SFP		0x03 /* SFP/SFP+/SFP28 */
#define SFF8024_ID_300PIN_XBI	0x04 /* 300 pin XBI */
#define SFF8024_ID_XENPAK	0x05
#define SFF8024_ID_XFP		0x06
#define SFF8024_ID_XFF		0x07
#define SFF8024_ID_XFPE		0x08 /* XFP-E */
#define SFF8024_ID_XPAK		0x09
#define SFF8024_ID_X2		0x0a
#define SFF8024_ID_DWDM_SFP	0x0b /* DWDM-SFP/SFP+ */
				     /* not using SFF-8472 */
#define SFF8024_ID_QSFP		0x0c
#define SFF8024_ID_QSFP_PLUS	0x0d /* or later */
				     /* using SFF-8436/8665/8685 et al */
#define SFF8024_ID_CXP		0x0e /* or later */
#define SFF8024_ID_HD4X		0x0f /* shielded mini multilane HD 4X */
#define SFF8024_ID_HD8X		0x10 /* shielded mini multilane HD 8X */
#define SFF8024_ID_QSFP28	0x11 /* or later */
				     /* using SFF-8665 et al */
#define SFF8024_ID_CXP2		0x12 /* aka CXP28, or later */
#define SFF8024_ID_CDFP		0x13 /* style 1/style 2 */
#define SFF8024_ID_HD4X_FAN	0x14 /* shielded mini multilane HD 4X fanout */ 
#define SFF8024_ID_HD8X_FAN	0x15 /* shielded mini multilane HD 8X fanout */ 
#define SFF8024_ID_CDFP3	0x16 /* style 3 */
#define SFF8024_ID_uQSFP	0x17 /* microQSFP */
#define SFF8024_ID_QSFP_DD	0x18 /* QSFP-DD double density 8x */
				     /* INF-8628 */
#define SFF8024_ID_RESERVED	0x7f /* up to here is reserved */
				     /* 0x80 to 0xff is vendor specific */

#define SFF8024_ID_IS_RESERVED(_id)	((_id) <= SFF8024_ID_RESERVED)
#define SFF8024_ID_IS_VENDOR(_id)	((_id) > SFF8024_ID_RESERVED)

#define SFF8024_CON_UNKNOWN	0x00
#define SFF8024_CON_SC		0x01 /* Subscriber Connector */
#define SFF8024_CON_FC_1	0x02 /* Fibre Channel Style 1 copper */
#define SFF8024_CON_FC_2	0x03 /* Fibre Channel Style 2 copper */
#define SFF8024_CON_BNC_TNC	0x04 /* BNC/TNC */
#define SFF8024_CON_FC_COAX	0x05 /* Fibre Channel coax headers */
#define SFF8024_CON_FJ		0x06 /* Fibre Jack */
#define SFF8024_CON_LC		0x07 /* Lucent Connector */
#define SFF8024_CON_MT_RJ	0x08 /* Mechanical Transfer - Registered Jack */
#define SFF8024_CON_MU		0x09 /* Multiple Optical */
#define SFF8024_CON_SG		0x0a
#define SFF8024_CON_O_PIGTAIL	0x0b /* Optical Pigtail */
#define SFF8024_CON_MPO_1x12	0x0c /* Multifiber Parallel Optic 1x12 */
#define SFF8024_CON_MPO_2x16	0x0e /* Multifiber Parallel Optic 2x16 */
#define SFF8024_CON_HSSDC2	0x20 /* High Speed Serial Data Connector */
#define SFF8024_CON_Cu_PIGTAIL	0x21 /* Copper Pigtail */
#define SFF8024_CON_RJ45	0x22
#define SFF8024_CON_NO		0x23 /* No separable connector */
#define SFF8024_CON_MXC_2x16	0x24
#define SFF8024_CON_RESERVED	0x7f /* up to here is reserved */
				     /* 0x80 to 0xff is vendor specific */

#define SFF8024_CON_IS_RESERVED(_id)	((_id) <= SFF8024_CON_RESERVED)
#define SFF8024_CON_IS_VENDOR(_id)	((_id) > SFF8024_CON_RESERVED)

static const char *sff8024_id_names[] = {
	[SFF8024_ID_UNKNOWN]	= "Unknown",
	[SFF8024_ID_GBIC]	= "GBIC",
	[SFF8024_ID_SFP]	= "SFP",
	[SFF8024_ID_300PIN_XBI]	= "300 pin XBI",
	[SFF8024_ID_XENPAK]	= "XENPAK",
	[SFF8024_ID_XFP]	= "XFP",
	[SFF8024_ID_XFF]	= "XFF",
	[SFF8024_ID_XFPE]	= "XFPE",
	[SFF8024_ID_XPAK]	= "XPAK",
	[SFF8024_ID_X2]		= "X2",
	[SFF8024_ID_DWDM_SFP]	= "DWDM-SFP",
	[SFF8024_ID_QSFP]	= "QSFP",
	[SFF8024_ID_QSFP_PLUS]	= "QSFP+",
	[SFF8024_ID_CXP]	= "CXP",
	[SFF8024_ID_HD4X]	= "Shielded Mini Multilane HD 4X",
	[SFF8024_ID_HD8X]	= "Shielded Mini Multilane HD 8X",
	[SFF8024_ID_QSFP28]	= "QSFP28",
	[SFF8024_ID_CXP2]	= "CXP2",
	[SFF8024_ID_CDFP]	= "CDFP Style 1/2",
	[SFF8024_ID_HD4X_FAN]	= "Shielded Mini Multilane HD 4X Fanout Cable",
	[SFF8024_ID_HD8X_FAN]	= "Shielded Mini Multilane HD 8X Fanout Cable",
	[SFF8024_ID_CDFP3]	= "CDFP Style 3",
	[SFF8024_ID_uQSFP]	= "microQSFP",
	[SFF8024_ID_QSFP_DD]	= "QSFP Double-Density",
};

static const char *sff8024_con_names[] = {
	[SFF8024_CON_UNKNOWN]	= "Unknown",
	[SFF8024_CON_SC]	= "SC",
	[SFF8024_CON_FC_1]	= "Fibre Channel style 1",
	[SFF8024_CON_FC_2]	= "Fibre Channel style 2",
	[SFF8024_CON_BNC_TNC]	= "BNC/TNC",
	[SFF8024_CON_FC_COAX]	= "Fibre Channel coax headers",
	[SFF8024_CON_FJ]	= "Fiber Jack",
	[SFF8024_CON_LC]	= "LC",
	[SFF8024_CON_MT_RJ]	= "MT-RJ",
	[SFF8024_CON_MU]	= "MU",
	[SFF8024_CON_SG]	= "SG",
	[SFF8024_CON_O_PIGTAIL]	= "Optical Pigtail",
	[SFF8024_CON_MPO_1x12]	= "MPO 2x16",
	[SFF8024_CON_MPO_2x16]	= "MPO 1x12",
	[SFF8024_CON_HSSDC2]	= "HSSDC II",
	[SFF8024_CON_Cu_PIGTAIL]
				= "Copper Pigtail",
	[SFF8024_CON_RJ45]	= "RJ45",
	[SFF8024_CON_NO]	= "No separable connector",
	[SFF8024_CON_MXC_2x16]	= "MXC 2x16",
};

#define SFF8472_ID			0 /* SFF8027 for identifier values */
#define SFF8472_EXT_ID			1
#define SFF8472_EXT_ID_UNSPECIFIED		0x00
#define SFF8472_EXT_ID_MOD_DEF_1		0x01
#define SFF8472_EXT_ID_MOD_DEF_2		0x02
#define SFF8472_EXT_ID_MOD_DEF_3		0x03
#define SFF8472_EXT_ID_2WIRE			0x04
#define SFF8472_EXT_ID_MOD_DEF_5		0x05
#define SFF8472_EXT_ID_MOD_DEF_6		0x06
#define SFF8472_EXT_ID_MOD_DEF_7		0x07
#define SFF8472_CON			2 /* SFF8027 for connector values */
#define SFF8472_VENDOR_START		20
#define SFF8472_VENDOR_END		35
#define SFF8472_PRODUCT_START		40
#define SFF8472_PRODUCT_END		55
#define SFF8472_REVISION_START		56
#define SFF8472_REVISION_END		59
#define SFF8472_SERIAL_START		68
#define SFF8472_SERIAL_END		83
#define SFF8472_DATECODE		84
#define SFF8472_DDM_TYPE		92
#define SFF8472_DDM_TYPE_AVG_POWER		(1U << 3)
#define SFF8472_DDM_TYPE_CAL_EXT		(1U << 4)
#define SFF8472_DDM_TYPE_CAL_INT		(1U << 5)
#define SFF8472_DDM_TYPE_IMPL			(1U << 6)
#define SFF8472_COMPLIANCE		94
#define SFF8472_COMPLIANCE_NONE			0x00
#define SFF8472_COMPLIANCE_9_3			0x01 /* SFF-8472 Rev 9.3 */
#define SFF8472_COMPLIANCE_9_5			0x02 /* SFF-8472 Rev 9.5 */
#define SFF8472_COMPLIANCE_10_2			0x03 /* SFF-8472 Rev 10.2 */
#define SFF8472_COMPLIANCE_10_4			0x04 /* SFF-8472 Rev 10.4 */
#define SFF8472_COMPLIANCE_11_0			0x05 /* SFF-8472 Rev 11.0 */
#define SFF8472_COMPLIANCE_11_3			0x06 /* SFF-8472 Rev 11.3 */
#define SFF8472_COMPLIANCE_11_4			0x07 /* SFF-8472 Rev 11.4 */
#define SFF8472_COMPLIANCE_12_3			0x08 /* SFF-8472 Rev 12.3 */

/*
 * page 0xa2
 */
#define SFF8472_DDM_TEMP		96
#define SFF8472_DDM_VCC			98
#define SFF8472_DDM_TX_BIAS		100
#define SFF8472_DDM_TX_POWER		102
#define SFF8472_DDM_RX_POWER		104
#define SFF8472_DDM_LASER		106 /* laser temp/wavelength */
					    /* optional */
#define SFF8472_DDM_TEC			108 /* Measured TEC current */

#define SFF_TEMP_FACTOR		256.0
#define SFF_VCC_FACTOR		10000.0
#define SFF_BIAS_FACTOR		500.0
#define SFF_POWER_FACTOR	10000.0

static void	hexdump(const void *, size_t);
static int	if_sff8472(int, const char *, int, const struct if_sffpage *);

static const char *
sff_id_name(uint8_t id)
{
	const char *name = NULL;

	if (id < nitems(sff8024_id_names)) {
		name = sff8024_id_names[id];
		if (name != NULL)
			return (name);
	}

	if (SFF8024_ID_IS_VENDOR(id))
		return ("Vendor Specific");

	return ("Reserved");
}

static const char *
sff_con_name(uint8_t id)
{
	const char *name = NULL;

	if (id < nitems(sff8024_con_names)) {
		name = sff8024_con_names[id];
		if (name != NULL)
			return (name);
	}

	if (SFF8024_CON_IS_VENDOR(id))
		return ("Vendor Specific");

	return ("Reserved");
}

static void
if_sffpage_init(struct if_sffpage *sff, const char *ifname,
    uint8_t addr, uint8_t page)
{
	memset(sff, 0, sizeof(*sff));

	if (strlcpy(sff->sff_ifname, ifname, sizeof(sff->sff_ifname)) >=
	    sizeof(sff->sff_ifname))
		errx(1, "interface name too long");

	sff->sff_addr = addr;
	sff->sff_page = page;
}

static void
if_sffpage_dump(const char *ifname, const struct if_sffpage *sff)
{
	printf("%s: addr %02x", ifname, sff->sff_addr);
	if (sff->sff_addr == IFSFF_ADDR_EEPROM)
		printf(" page %u", sff->sff_page);
	putchar('\n');
	hexdump(sff->sff_data, sizeof(sff->sff_data));
}

int
if_sff_info(int s, const char *ifname, int dump)
{
	struct if_sffpage pg0;
	int error = 0;
	uint8_t id, ext_id;

	if_sffpage_init(&pg0, ifname, IFSFF_ADDR_EEPROM, 0);

	if (ioctl(s, SIOCGIFSFFPAGE, (caddr_t)&pg0) == -1)
		return (-1);

	if (dump)
		if_sffpage_dump(ifname, &pg0);

	id = pg0.sff_data[0]; /* SFF8472_ID */

	printf("%s: identifier %s (%02x)\n", ifname, sff_id_name(id), id);
	switch (id) {
	case SFF8024_ID_SFP:
		ext_id = pg0.sff_data[SFF8472_EXT_ID];
		if (ext_id != SFF8472_EXT_ID_2WIRE) {
			printf("\textended-id: %02xh\n", ext_id);
			break;
		}
		/* FALLTHROUGH */
	case SFF8024_ID_GBIC:
		error = if_sff8472(s, ifname, dump, &pg0);
		break;
	}

	return (error);
}

static void
if_sff_ascii_print(const struct if_sffpage *sff,
    size_t start, size_t end)
{
	const uint8_t *d = sff->sff_data;
	int ch;

	for (;;) {
		ch = d[start];
		if (!isspace(ch) && ch != '\0')
			break;

		start++;
		if (start == end) {
			printf("(unknown)\n");
			return;
		}
	}

	for (;;) {
		int ch = d[end];
		if (!isspace(ch) && ch != '\0')
			break;

		end--;
	}

	do {
		char dst[8];
		vis(dst, d[start], VIS_TAB | VIS_NL, 0);
		printf("%s", dst);
	} while (++start <= end);
}

static void
if_sff_date_print(const struct if_sffpage *sff,
    size_t start)
{
	const uint8_t *d = sff->sff_data + start;
	size_t i;

	/* YYMMDD */
	for (i = 0; i < 6; i++) {
		if (!isdigit(d[i])) {
			if_sff_ascii_print(sff, start, start + 5);
			return;
		}
	}

	printf("20%c%c-%c%c-%c%c\n",
	    d[0], d[1], d[2], d[3], d[4], d[5]);
}

static int16_t
if_sff_int(const struct if_sffpage *sff, size_t start)
{
	const uint8_t *d = sff->sff_data + start;

	return (d[0] << 8 | d[1]);
}

static uint16_t
if_sff_uint(const struct if_sffpage *sff, size_t start)
{
	const uint8_t *d = sff->sff_data + start;

	return (d[0] << 8 | d[1]);
}

static float
if_sff_power2dbm(uint16_t power)
{
	return (10.0 * log10f((float)power / 10000.0));
}

static int
if_sff8472(int s, const char *ifname, int dump, const struct if_sffpage *pg0)
{
	struct if_sffpage ddm;
	uint8_t ddm_types;

	if (dump) {
		uint8_t con = pg0->sff_data[SFF8472_CON];
		printf("\tconnector: %s (%02x)\n", sff_con_name(con), con);
	}

	printf("\tvendor: ");
	if_sff_ascii_print(pg0, SFF8472_VENDOR_START, SFF8472_VENDOR_END);
	printf(", product: ");
	if_sff_ascii_print(pg0, SFF8472_PRODUCT_START, SFF8472_PRODUCT_END);
	printf(", rev: ");
	if_sff_ascii_print(pg0, SFF8472_REVISION_START, SFF8472_REVISION_END);
	printf("\n\tserial: ");
	if_sff_ascii_print(pg0, SFF8472_SERIAL_START, SFF8472_SERIAL_END);
	printf(", date: ");
	if_sff_date_print(pg0, SFF8472_DATECODE);

	ddm_types = pg0->sff_data[SFF8472_DDM_TYPE];
	if (pg0->sff_data[SFF8472_COMPLIANCE] == SFF8472_COMPLIANCE_NONE ||
	    !ISSET(ddm_types, SFF8472_DDM_TYPE_IMPL))
		return (0);

	if_sffpage_init(&ddm, ifname, IFSFF_ADDR_DDM, 0);
	if (ioctl(s, SIOCGIFSFFPAGE, (caddr_t)&ddm) == -1)
		return (-1);

	if (dump)
		if_sffpage_dump(ifname, &ddm);

	if (ISSET(ddm_types, SFF8472_DDM_TYPE_CAL_EXT)) {
		printf("\tcalibration: external "
		    "(WARNING: needs more code)\n");
	}

	printf("\ttemperature: %.02f C",
	    if_sff_int(&ddm, SFF8472_DDM_TEMP) / SFF_TEMP_FACTOR);
	printf(", vcc: %.02f V\n",
	    if_sff_uint(&ddm, SFF8472_DDM_VCC) / SFF_VCC_FACTOR);
	printf("\ttx-bias: %.02f mA",
	    if_sff_uint(&ddm, SFF8472_DDM_TX_BIAS) / SFF_BIAS_FACTOR);
	printf(", tx-power: %.02f dBm",
	    if_sff_power2dbm(if_sff_uint(&ddm, SFF8472_DDM_TX_POWER)));
	printf(", rx-power: %.02f dBm %s\n",
	    if_sff_power2dbm(if_sff_uint(&ddm, SFF8472_DDM_RX_POWER)),
	    ISSET(ddm_types, SFF8472_DDM_TYPE_AVG_POWER) ? "avg" : "OMA");

	return (0);
}

static int
printable(int ch)
{
	if (ch == '\0')
		return ('_');
	if (!isprint(ch))
		return ('~');

	return (ch);
}

static void
hexdump(const void *d, size_t datalen)
{
	const uint8_t *data = d;
	int i, j = 0;

	for (i = 0; i < datalen; i += j) {
		printf("% 4d: ", i);
		for (j = 0; j < 16 && i+j < datalen; j++)
			printf("%02x ", data[i + j]);
		while (j++ < 16)
			printf("   ");
		printf("|");
		for (j = 0; j < 16 && i+j < datalen; j++)
			putchar(printable(data[i + j]));
		printf("|\n");
	}
}

#endif /* SMALL */
