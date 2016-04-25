// config options for UDP sync
extern int udp_master;
extern int udp_slave;
extern int udp_port;
extern const char *udp_ip; // where the master sends datagrams
                           // (can be a broadcast address)
extern float udp_seek_threshold; // how far off before we seek

void send_udp(const char *send_to_ip, int port, char *mesg);
double get_master_clock_udp(double pts);