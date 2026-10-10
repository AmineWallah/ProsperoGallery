#ifndef INSTANCE_H
#define INSTANCE_H

// Names this process and stops any older copy of the gallery that's still
// running, so sending a new build replaces the old one instead of failing
// with "port is busy". Does nothing on PC.
void instance_take_over(void);

#endif
