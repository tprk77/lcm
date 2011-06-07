// file: send_message.cpp
//
// LCM example program.
//
// compile with:
//  $ g++ -o send_message send_message.cpp -llcm
//
// If using GNU/Linux, you can also use pkg-config:
//  $ g++ -o send_message send_message.cpp `pkg-config --cflags --libs lcm`

#include <stdio.h>
#include <lcm/lcm.h>

#include "exlcm/example_t.hpp"

int
main(int argc, char ** argv)
{
    lcm::LCM lcm;

    if(0 != lcm.init())
        return 1;

    exlcm::example_t my_data;
    my_data.timestamp = 0;

    my_data.position[0] = 1;
    my_data.position[1] = 2;
    my_data.position[2] = 3;

    my_data.orientation[0] = 1;
    my_data.orientation[1] = 0;
    my_data.orientation[2] = 0;
    my_data.orientation[3] = 0;

    my_data.num_ranges = 15;
    my_data.ranges.resize(my_data.num_ranges);
    for(int i = 0; i < my_data.num_ranges; i++)
        my_data.ranges[i] = i;

    lcm.publish("EXAMPLE", &my_data);

    return 0;
}
