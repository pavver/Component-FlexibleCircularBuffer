#include "FlexibleCircularBuffer.h"


/// @brief add data to last line. if char data type then remove \0 simbol
/// @param id id of the last line of data, in order to make sure that the data will be written exactly in the required line, if a new line was created and you try to write in the old line, 0 will be returned
/// @param data data
/// @param length data length
/// @return id of the created line. 0 if error
template <>
uint32_t FlexibleCircularBuffer<char>::WriteToLastLine(uint32_t id, const char *data, uint16_t length)
{
  if (_indexLastLine == -1 || id != lines[_indexLastLine].id ||
      calculateLineLength(lines[_indexLastLine]) + length > _bufferSize / 2)
    return 0;

  // if the data fits into the buffer without fragmentation, then we simply write it to the buffer
  if ((lines[_indexLastLine].endIndex + length) < _bufferSize)
  {
    memcpy(buff + lines[_indexLastLine].endIndex, data, length * sizeof(char));
    lines[_indexLastLine].endIndex = lines[_indexLastLine].endIndex + length - 1;
  }
  else
  {
    // Otherwise, we need to split the line.
    memcpy(buff + lines[_indexLastLine].endIndex, data,
           (_bufferSize - lines[_indexLastLine].endIndex - 1) * sizeof(char));
    lines[_indexLastLine].endIndex = _bufferSize - lines[_indexLastLine].endIndex - 2;
    data += lines[_indexLastLine].endIndex;
    length -= lines[_indexLastLine].endIndex + 1;

    // Write the rest of the data to a new line.
    memcpy(buff, data, length * sizeof(char));
    lines[_indexLastLine].endIndex = length - 1;
  }

  FixIntersection(lines[_indexLastLine]);

  return id;
}
