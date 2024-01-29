#pragma once

#ifndef FlexibleCircularBuffer_h
#define FlexibleCircularBuffer_h

#include <cstring>

#define FreeRTOS 1
#define ThreadSafe FreeRTOS

#ifdef ThreadSafe
#if ThreadSafe == FreeRTOS

#include "freertos/FreeRTOS.h"
#include <freertos/semphr.h>
#include <freertos/task.h>

#endif
#endif

// #define DebugMode_FlexibleCircularBuffer

#ifdef DebugMode_FlexibleCircularBuffer
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#endif

template <typename BuffT>
struct BufferLine
{
public:
  /// Get the data
  const BuffT *GetData() const
  {
    return _data;
  }

  /// Get the length
  uint16_t GetLength() const
  {
    return _length;
  }

  /// Get the id
  uint32_t GetId() const
  {
    return _id;
  }

  /// Destructor
  ~BufferLine()
  {
    free(_data);
  }

protected:
  /// Constructor
  BufferLine(BuffT *data, uint16_t length, uint32_t id)
      : _data(data),
        _id(id),
        _length(length)
  {
  }

  /// The data
  BuffT *_data;
  /// The id
  const uint32_t _id;
  /// The length data
  const uint16_t _length;
};

/// @brief Buffer line with editable content.
/// @tparam BuffT Type of the buffer.
template <typename BuffT>
class EditableBufferLine : public BufferLine<BuffT>
{
public:
  /// @brief Constructor.
  /// @param data from buffer line.
  /// @param Length of the buffer line.
  /// @param id Identifier of the line.
  EditableBufferLine(BuffT *data, uint16_t length, uint32_t id)
      : BufferLine<BuffT>(data, length, id)
  {
  }
};

struct BufferLineMarker
{
public:
  /// Start index of the line in buffer data.
  int16_t startIndex = 0;
  /// End index of the line in buffer data.
  int16_t endIndex = 0;
  /// Identifier of the line.
  uint32_t id = 0;

  /// @brief Check if the line intersects with the given line.
  bool inIntersection(BufferLineMarker &line)
  {
    // Check if both lines are not fragmented.
    if (startIndex < endIndex && line.startIndex < line.endIndex)
    {
      // If the start index is less than the line start index, the intersection
      // is at the start of the line.
      if (startIndex <= line.startIndex)
        return endIndex >= line.startIndex;
      // Otherwise, the intersection is at the end of the line.
      return line.endIndex >= startIndex;
    }
    // if both lines are fragmented, then they exactly intersect.
    if (startIndex > endIndex && line.startIndex > line.endIndex)
      return true;
    // we check depending on which of the lines is fragmented
    if (startIndex > endIndex)
      return line.startIndex <= endIndex || line.endIndex >= startIndex;
    return startIndex <= line.endIndex || endIndex >= line.startIndex;
  }
};

/// @brief circular buffer for data of different lengths
/// @tparam BuffT Type of the buffer.
template <typename BuffT>
class FlexibleCircularBuffer
{
public:
  FlexibleCircularBuffer(uint16_t bufferSize = 4096, uint16_t maxLines = 128)
      : _bufferSize(bufferSize),
        _maxLines(maxLines)
  {
#ifdef ThreadSafe
#if ThreadSafe == FreeRTOS
    sync_mutex = xSemaphoreCreateMutex();
#endif
#endif

    buff = new BuffT[_bufferSize];
    lines = new BufferLineMarker[_maxLines];
  }

  ~FlexibleCircularBuffer()
  {
    delete[] buff;
    delete[] lines;
  }

  /// @brief Write new line to buffer
  /// @param data data
  /// @param length data length
  /// @return id of the created line. 0 if error
  uint32_t WriteLine(const BuffT *data, uint16_t length)
  {
    // If the length of the data is 0, return 0.
    if (length == 0)
      return 0;

    // for the correct operation of the algorithm, there must always be at least two active lines in the buffer.
    // therefore, we check that the new lines data does not occupy more than half of the buffer.
    if (length > _bufferSize / 2)
      return 0;

    sync_lock();

    // Get the next index to write to.
    int16_t nextIndex = getNextIndex(_indexLastLine);

    // Create a new line.
    BufferLineMarker newLine;
    newLine.endIndex = 0;

    // if the data fits into the buffer without fragmentation, then we simply write it to the buffer
    if ((lines[_indexLastLine].endIndex + length + 1) < _bufferSize)
    {
      memcpy(_indexLastLine == -1 ? buff : (buff + lines[_indexLastLine].endIndex + 1), data, length * sizeof(BuffT));
      newLine.endIndex = (_indexLastLine == -1 ? 0 : lines[_indexLastLine].endIndex + 1) + length - 1;
    }
    else
    {
      // Otherwise, we need to split the line.
      memcpy(_indexLastLine == -1 ? buff : (buff + lines[_indexLastLine].endIndex + 1),
             data,
             (_bufferSize - lines[_indexLastLine].endIndex - 1) * sizeof(BuffT));
      newLine.endIndex = _bufferSize - lines[_indexLastLine].endIndex - 1;
      data += newLine.endIndex;
      length -= newLine.endIndex;

      // Write the rest of the data to a new line.
      memcpy(buff, data, length * sizeof(BuffT));
      newLine.endIndex = length - 1;
    }

    // If this is the first line, then it is the first line.
    if (_indexLastLine == -1)
    {
      newLine.startIndex = 0;
      newLine.id = 0;
    }
    else
    {
      // Otherwise, we need to find the start index of the new line.
      newLine.startIndex = (lines[_indexLastLine].endIndex + 1) % _bufferSize;
      newLine.id = lines[_indexLastLine].id + 1;
    }

    // if the data of the line we just recorded intersects with the previously recorded lines,
    // then we erase the data about the overwritten lines.
    if (_indexFirstLine == -1)
      _indexFirstLine = nextIndex;
    else
      FixIntersection(newLine);

    // Set the new line as the last line.
    _indexLastLine = nextIndex;
    lines[_indexLastLine] = newLine;

    sync_unlock();

    // Return the id of the new line.
    return newLine.id;
  }

  /// @brief add data to last line
  /// @param id id of the last line of data, in order to make sure that the data will be written exactly in the required line, if a new line was created and you try to write in the old line, 0 will be returned
  /// @param data data
  /// @param length data length
  /// @return id of the created line. 0 if error
  uint32_t WriteToLastLine(uint32_t id, const BuffT *data, uint16_t length)
  {
    if (_indexLastLine == -1 || id != lines[_indexLastLine].id ||
        calculateLineLength(lines[_indexLastLine]) > _bufferSize / 2)
      return 0;

    // if the data fits into the buffer without fragmentation, then we simply write it to the buffer
    if ((lines[_indexLastLine].endIndex + length + 1) < _bufferSize)
    {
      memcpy(buff + lines[_indexLastLine].endIndex + 1, data, length * sizeof(BuffT));
      lines[_indexLastLine].endIndex = lines[_indexLastLine].endIndex + length;
    }
    else
    {
      // Otherwise, we need to split the line.
      memcpy(buff + lines[_indexLastLine].endIndex + 1, data,
             (_bufferSize - lines[_indexLastLine].endIndex - 1) * sizeof(BuffT));
      lines[_indexLastLine].endIndex = _bufferSize - lines[_indexLastLine].endIndex - 1;
      data += lines[_indexLastLine].endIndex;
      length -= lines[_indexLastLine].endIndex;

      // Write the rest of the data to a new line.
      memcpy(buff, data, length * sizeof(BuffT));
      lines[_indexLastLine].endIndex = length - 1;
    }

    FixIntersection(lines[_indexLastLine]);

    return id;
  }

  /// @brief Read the first buffer line
  /// @return BufferLine or nullptr if empty
  BufferLine<BuffT> *ReadFirst()
  {
    if (_indexFirstLine < 0)
      return nullptr;

    sync_lock();
    BufferLine<BuffT> *ret = CreateBufferLine(_indexFirstLine);
    sync_unlock();
    return ret;
  }

  /// @brief Read the last buffer line
  /// @return BufferLine or nullptr if empty
  BufferLine<BuffT> *ReadLast()
  {
    if (_indexLastLine < 0)
      return nullptr;

    sync_lock();
    BufferLine<BuffT> *ret = CreateBufferLine(_indexLastLine);
    sync_unlock();
    return ret;
  }

  /// @brief Read the next buffer line with given id
  /// @return BufferLine or nullptr if not found
  BufferLine<BuffT> *ReadNext(uint32_t id)
  {
    if (_indexLastLine < 0)
      return nullptr;

    sync_lock();
    BufferLine<BuffT> *ret = nullptr;

    // Find the next line with given id
    for (int16_t index = _indexFirstLine; index != _indexLastLine; index = getNextIndex(index))
    {
      // If the current line has the given id, return it
      if (lines[index].id == id)
      {
        ret = CreateBufferLine(getNextIndex(index));
        break;
      }
    }

    sync_unlock();

    // If there are no more lines, return nullptr
    return ret;
  }

  // Delete the current one, and return next line.
  BufferLine<BuffT> *FreeAndReadNext(BufferLine<BuffT> *line)
  {
    // Get the ID of the line
    uint32_t id = line->GetId();
    // Delete the current line
    delete line;
    // Return the next line with the same ID
    return ReadNext(id);
  }

#ifdef DebugMode_FlexibleCircularBuffer

  void SnapshotToFile(const char *outFileName, const char *firstHtmlFilePath, const char *lastHtmlFilePath)
  {
    std::ofstream file;
    file.open(outFileName);
    typename std::filesystem::path currentPath = std::filesystem::current_path();
    typename std::filesystem::path fullPath = currentPath / outFileName;
    std::cout << "Snapshot save to: " << fullPath << std::endl;
    copyContent(file, firstHtmlFilePath);
    file << "  <p style=\"margin: 4px;\">BufferSize: " << _bufferSize << ", MaxLines: " << _maxLines << "</p>\n";
    file << "  <p style=\"margin: 4px;\">Buffer cells:</p>\n";
    pushBuffer(file);
    file << "  <p style=\"margin: 4px;\">IndexFirstLine: " << _indexFirstLine << ", IndexLastLine: " << _indexLastLine << "</p>\n";
    file << "  <p style=\"margin: 4px;\">Lines:</p>";
    pushLines(file);
    copyContent(file, lastHtmlFilePath);
    file.close();
  }

#endif

private:
  // Pointer to the buffer
  BuffT *buff;
  // Pointer to the buffer line marker
  BufferLineMarker *lines;

  // Buffer size
  const uint16_t _bufferSize;
  // Maximum number of lines
  const uint16_t _maxLines;

  // Index of the first line
  int16_t _indexFirstLine = -1;

  // Index of the last line
  int16_t _indexLastLine = -1;

#ifdef DebugMode_FlexibleCircularBuffer

  void copyContent(std::ofstream &to, const char *fromFilePath)
  {
    std::ifstream file(fromFilePath);

    if (file.is_open())
    {
      std::string line;
      while (std::getline(file, line))
      {
        to << line << std::endl;
      }
      file.close();
    }
  }

  bool cellExistInLine(uint16_t cell, BufferLineMarker &line)
  {
    if (line.startIndex < line.endIndex)
      return line.startIndex <= cell && cell <= line.endIndex;
    return cell <= line.endIndex || line.startIndex <= cell;
  }

  BufferLineMarker *GetLineByCell(uint16_t cell)
  {
    if (_indexLastLine < 0)
      return nullptr;

    for (int16_t index = _indexFirstLine; true; index = getNextIndex(index))
    {
      if (cellExistInLine(cell, lines[index]))
        return &lines[index];
      if (index == _indexLastLine)
        break;
    }

    return nullptr;
  }

  void pushBuffer(std::ofstream &file)
  {
    const uint16_t cellsInRow = 40;
    uint16_t countRows = _bufferSize / cellsInRow;

    BufferLineMarker *currentLine = nullptr;

    file << "  <table id=\"Buffer\">\n";

    for (uint16_t i = 0; i < _bufferSize; i++)
    {

      if (i > 0 && ((i % cellsInRow) == 0 || i == _bufferSize))
        file << "    </tr>\n";

      if ((i % cellsInRow) == 0)
        file << "    <tr>\n";

      if (currentLine == nullptr)
        currentLine = GetLineByCell(i);
      else if (!cellExistInLine(i, *currentLine))
        currentLine = GetLineByCell(i);

      file << "      <td class=\"buffer-cell";
      if (currentLine != nullptr)
      {
        if (currentLine->startIndex == i)
          file << " buffer-first-line-cell";
        if (currentLine->endIndex == i)
          file << " buffer-last-line-cell";
        file << " color-" << currentLine->id % 10;
      }
      file << "\"><span>";
      outBuffT(file, buff[i]);
      file << "</span></td>\n";
    }

    file << "  </table>";
  }

  void outBuffT(std::ofstream &file, BuffT &cell)
  {
    file << cell;
  }

  void pushBuffT(std::ofstream &file, BuffT *data, uint16_t length)
  {
    for (uint16_t i = 0; i < length; i++)
    {
      file << outBuffT(file, data[i]);
      if (i < length - 1)
        file << ", ";
    }
  }

  void pushLines(std::ofstream &file)
  {
    file << "  <table id=\"Lines\">\n";
    file << "    <tr>\n";
    file << "      <th><span>index</span></th>\n";
    file << "      <th><span>id</span></th>\n";
    file << "      <th><span>startIndex</span></th>\n";
    file << "      <th><span>endIndex</span></th>\n";
    file << "      <th><span>Length</span></td>\n";
    file << "      <th><span>data</span></th>\n";
    file << "    </tr>\n";

    for (uint16_t i = 0; i < _maxLines; i++)
    {
      bool isActiveLine = false;
      if (_indexLastLine >= 0)
      {
        if (_indexFirstLine <= _indexLastLine)
          isActiveLine = i <= _indexLastLine && i >= _indexFirstLine;
        else
          isActiveLine = i <= _indexLastLine || _indexFirstLine <= i;
      }

      uint16_t length = 0;
      BuffT *lineData = nullptr;
      if (isActiveLine)
        lineData = AllocLineData(lines[i], length);

      file << "    <tr";
      if (isActiveLine)
        file << " class=\"color-" << lines[i].id % 10 << "\"";
      file << ">\n";
      file << "      <td><span>" << i << "</span></td>\n";
      file << "      <td><span>" << lines[i].id << "</span></td>\n";
      file << "      <td><span>" << lines[i].startIndex << "</span></td>\n";
      file << "      <td><span>" << lines[i].endIndex << "</span></td>\n";
      file << "      <td><span>" << length << "</span></td>\n";
      file << "      <td><span>";
      if (isActiveLine)
        pushBuffT(file, lineData, length);
      file << "</span></td>\n";
      file << "    </tr>\n";

      free(lineData);
    }
  }

#endif

  /// @brief Get the next index in the text buffer.
  /// @param[in] index The current index.
  int16_t getNextIndex(int16_t index)
  {
    // Return the next index in the buffer.
    return (index + 1) % _maxLines;
  }

  /// @brief Get the previous index in the text buffer.
  /// @param[in] index The current index.
  int16_t getPrevIndex(int16_t index)
  {
    // Return the previous index in the buffer.
    return (index + _maxLines - 1) % _maxLines;
  };

  void FixIntersection(BufferLineMarker &newLine)
  {
    BufferLineMarker *line = &lines[_indexFirstLine];
    bool InIntersection = line->inIntersection(newLine);
    while (InIntersection)
    {
      _indexFirstLine = getNextIndex(_indexFirstLine);
      line = &lines[_indexFirstLine];
      InIntersection = line->inIntersection(newLine);
    }
  }

  uint16_t calculateLineLength(BufferLineMarker &line)
  {
    if (line.startIndex < line.endIndex)
      return line.endIndex - line.startIndex + 1;
    return _bufferSize - line.startIndex + line.endIndex + 1;
  }

  BuffT *AllocLineData(BufferLineMarker &line, uint16_t &length)
  {
    length = calculateLineLength(line);
    BuffT *lineData = (BuffT *)malloc(sizeof(BuffT) * length);
    // If the line is not fragmented
    if (line.startIndex < line.endIndex)
    {
      memcpy(lineData, buff + line.startIndex, length * sizeof(BuffT));
      return lineData;
    }
    // Otherwise, create a new line from fragments.
    memcpy(lineData, buff + line.startIndex, (_bufferSize - line.startIndex) * sizeof(BuffT));
    memcpy(lineData + _bufferSize - line.startIndex, buff, (line.endIndex + 1) * sizeof(BuffT));
    return lineData;
  }

  BufferLine<BuffT> *CreateBufferLine(int16_t index)
  {
    uint16_t length = 0;
    BuffT *lineData = AllocLineData(lines[index], length);
    return new EditableBufferLine<BuffT>(lineData, length, lines[index].id);
  }

  // FreeRTOS Thread sync mutex.
#ifdef ThreadSafe
#if ThreadSafe == FreeRTOS
  SemaphoreHandle_t sync_mutex = nullptr;
#endif
#endif

  /// @brief Thread lock
  bool sync_lock()
  {
#ifdef ThreadSafe
#if ThreadSafe == FreeRTOS
    return sync_mutex ? (xSemaphoreTake(sync_mutex, portMAX_DELAY) == pdTRUE) : false;
#else
    return true;
#endif
#endif
  }

  /// @brief Thread unlock
  void sync_unlock()
  {
#ifdef ThreadSafe
#if ThreadSafe == FreeRTOS
    xSemaphoreGive(sync_mutex);
#endif
#endif
  }
};

#ifdef DebugMode_FlexibleCircularBuffer

template <>
void FlexibleCircularBuffer<char>::outBuffT(std::ofstream &file, char &cell)
{
  if (cell == '\0')
    file << "\\0";
  else if (cell == '\n')
    file << "\\n";
  else if (cell == '\r')
    file << "\\r";
  else if (cell == '\t')
    file << "\\t";
  else
    file << cell;
}

template <>
void FlexibleCircularBuffer<char>::pushBuffT(std::ofstream &file, char *data, uint16_t length)
{
  for (uint16_t i = 0; i < length; i++)
    outBuffT(file, data[i]);
}

#endif

#endif