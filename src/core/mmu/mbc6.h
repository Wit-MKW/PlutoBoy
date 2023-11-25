#ifndef MBC6_H
#define MBC6_H


void setup_MBC6(int flags);
uint8_t read_MBC6(uint16_t addr);
void   write_MBC6(uint16_t addr, uint8_t val);

#endif //MBC6_H
