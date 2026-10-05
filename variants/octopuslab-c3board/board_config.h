#ifndef BOARD_CONFIG_H_
#define BOARD_CONFIG_H_

// Smart Cable GPIOs
#define CABLE_CLK_IN   8 // LPT Paper Error (pin 12, byte S5)
#define CABLE_DATA_IN  6 // LPT Select      (pin 13, byte S4)
#define CABLE_CLK_OUT  5 // LPT Data 1 (pin 3)
#define CABLE_DATA_OUT 7 // LPT Data 0 (pin 2)

#define LED2 4
#define LED3 10

inline bool boardBegin() {
    return true;
}

#endif
