#ifndef STORAGE_H
#define STORAGE_H

#include "display_comms.h"
#include <Arduino.h>

namespace Storage {

bool init();
void loadMoulds(DisplayComms::MouldParams *moulds, int &count, int maxCount);
void saveMoulds(const DisplayComms::MouldParams *moulds, int count);

} // namespace Storage

#endif // STORAGE_H
