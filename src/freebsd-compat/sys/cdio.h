/* FreeBSD 9 <sys/cdio.h>, the subset dlls/ntdll/unix/cdrom.c uses.
 * 133 of the 307 errors were this one header. */
#ifndef _SYS_CDIO_H_
#define _SYS_CDIO_H_
#include <sys/ioccom.h>
#include <sys/types.h>

/* Address formats. */
#define CD_LBA_FORMAT   1
#define CD_MSF_FORMAT   2

/* Sub-channel data formats. */
#define CD_SUBQ_DATA        0
#define CD_CURRENT_POSITION 1
#define CD_MEDIA_CATALOG    2
#define CD_TRACK_INFO       3

union msf_lba {
    struct { u_char unused; u_char minute; u_char second; u_char frame; } msf;
    int       lba;
    u_char    addr[4];
};

struct cd_toc_entry {
    u_char          nothing1;
#if BYTE_ORDER == LITTLE_ENDIAN
    u_char          control:4;
    u_char          addr_type:4;
#else
    u_char          addr_type:4;
    u_char          control:4;
#endif
    u_char          track;
    u_char          nothing2;
    union msf_lba   addr;
};

struct cd_sub_channel_header {
    u_char  nothing1;
    u_char  audio_status;
    u_char  data_len[2];
};

struct cd_sub_channel_position_data {
    u_char          data_format;
#if BYTE_ORDER == LITTLE_ENDIAN
    u_char          control:4;
    u_char          addr_type:4;
#else
    u_char          addr_type:4;
    u_char          control:4;
#endif
    u_char          track_number;
    u_char          index_number;
    union msf_lba   absaddr;
    union msf_lba   reladdr;
};

struct cd_sub_channel_media_catalog {
    u_char  data_format;
    u_char  nothing1, nothing2, nothing3;
#if BYTE_ORDER == LITTLE_ENDIAN
    u_char  mc_valid:1;
    u_char  reserved:7;
#else
    u_char  reserved:7;
    u_char  mc_valid:1;
#endif
    u_char  mc_number[15];
};

struct cd_sub_channel_track_info {
    u_char  data_format;
    u_char  nothing1;
    u_char  track_number;
    u_char  nothing2;
#if BYTE_ORDER == LITTLE_ENDIAN
    u_char  ti_valid:1;
    u_char  reserved:7;
#else
    u_char  reserved:7;
    u_char  ti_valid:1;
#endif
    u_char  ti_number[15];
};

struct cd_sub_channel_info {
    struct cd_sub_channel_header header;
    union {
        struct cd_sub_channel_position_data   position;
        struct cd_sub_channel_media_catalog   media_catalog;
        struct cd_sub_channel_track_info      track_info;
    } what;
};

struct ioc_read_subchannel {
    u_char  address_format;
    u_char  data_format;
    u_char  track;
    int     data_len;
    struct cd_sub_channel_info *data;
};

struct ioc_toc_header {
    u_short len;
    u_char  starting_track;
    u_char  ending_track;
};

struct ioc_read_toc_entry {
    u_char  address_format;
    u_char  starting_track;
    u_short data_len;
    struct cd_toc_entry *data;
};

#define CDIOCREADSUBCHANNEL _IOWR('c', 3, struct ioc_read_subchannel)
#define CDIOREADTOCHEADER   _IOR('c', 4, struct ioc_toc_header)
#define CDIOREADTOCENTRYS   _IOWR('c', 5, struct ioc_read_toc_entry)
#define CDIOCPREVENT        _IO('c', 10)
#define CDIOCALLOW          _IO('c', 11)
#define CDIOCRESET          _IO('c', 20)
#define CDIOCEJECT          _IO('c', 24)
#define CDIOCCLOSE          _IO('c', 28)

/* Audio status, returned in cd_sub_channel_header.audio_status. */
#define CD_AS_AUDIO_INVALID     0x00
#define CD_AS_PLAY_IN_PROGRESS  0x11
#define CD_AS_PLAY_PAUSED       0x12
#define CD_AS_PLAY_COMPLETED    0x13
#define CD_AS_PLAY_ERROR        0x14
#define CD_AS_NO_STATUS         0x15

struct ioc_play_msf {
    u_char  start_m, start_s, start_f;
    u_char  end_m,   end_s,   end_f;
};

struct ioc_vol {
    u_char  vol[4];
};

#define CDIOCPLAYMSF    _IOW('c', 25, struct ioc_play_msf)
#define CDIOCGETVOL     _IOR('c', 18, struct ioc_vol)
#define CDIOCSETVOL     _IOW('c', 19, struct ioc_vol)
#define CDIOCPAUSE      _IO('c', 7)
#define CDIOCRESUME     _IO('c', 8)
#define CDIOCSTART      _IO('c', 22)
#define CDIOCSTOP       _IO('c', 23)

#endif /* _SYS_CDIO_H_ */
