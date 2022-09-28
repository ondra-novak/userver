/*
 * chunked_stream.h
 *
 *  Created on: 10. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_CHUNKED_STREAM_H_
#define SRC_LIBS_USERVER_CHUNKED_STREAM_H_
#include "stream.h"

namespace userver {

///Chunked stream handles transform encoding "chunked".
/**
 * Data writen to the stream are split to chunks as "chunked" specification states. Data read
 * from the stream are decoded, so chunk marks are removed from the stream
 *
 * Closing output of the chunked stream causes to write closing chunk. Reading closing chunk
 * causes emiting EOF state
 */

class ChunkedStream: public AbstractStreamInstance {
public:

    ChunkedStream(AbstractStreamInstance &stream, bool writing, bool reading);
    ChunkedStream(const ChunkedStream &other) = delete;
    ChunkedStream(ChunkedStream &&other);
    ChunkedStream &operator=(const ChunkedStream &other) = delete;
    ChunkedStream &operator=(ChunkedStream &&other) = delete;
    ~ChunkedStream();

    virtual void put_back(const std::string_view &buffer) override;
    virtual void timeout_async_write() override;
    virtual void read_async(Callback<void(const ReadData &)> &&callback) override;
    virtual bool write_async(const std::string_view &buffer,
                Callback<void(bool)> &&callback) override;
    virtual int get_read_timeout() const override;
    virtual ReadData read_sync() override;
    virtual std::string_view read_sync_nb() override;
    virtual void clear_timeout() override;
    virtual int get_write_timeout() const override;
    virtual void close_input() override;
    virtual void close_output() override;
    virtual void set_rw_timeout(int tm_in_ms) override;
    virtual void set_read_timeout(int tm_in_ms) override;
    virtual void set_write_timeout(int tm_in_ms) override;
    virtual bool write_sync(const std::string_view &buffer) override;
    virtual void timeout_async_read() override;



protected:
    StreamRef _ref;
    bool write_closed = false;
    bool read_closed = false;
    std::size_t chunk_size = 0;
    std::string_view putback_buff;
    std::vector<char> _chunk_out;
    


    static std::size_t parseChunkLine(const std::string_view &ln);
    void createChunk(const std::string_view &data);
};

}



#endif /* SRC_LIBS_USERVER_CHUNKED_STREAM_H_ */