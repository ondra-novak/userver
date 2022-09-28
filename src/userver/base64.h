/*
 * base64.h
 *
 *  Created on: 17. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_BASE64_H_iowuqiowdqo8923yd23uhfd42
#define SRC_USERVER_BASE64_H_iowuqiowdqo8923yd23uhfd42

#include <string_view>

namespace userver {

class Base64Table {
public:
    Base64Table(const std::string_view &charset, const std::string_view &trailer1, const std::string_view &trailer2)
    :charset(charset)
    ,trailer1(trailer1)
    ,trailer2(trailer2)
    {
        for (unsigned char &c: revtable) {
            c = 0x80;
        }

        for (std::size_t i = 0, cnt = charset.size(); i != cnt; ++i) {
            revtable[static_cast<int>(charset[i])] = i;
        }
    }

    std::string_view charset;
    std::string_view trailer1;
    std::string_view trailer2;
    unsigned char revtable[256];

    static const Base64Table &get_default_table() {
        static Base64Table tbl("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/","=","==");
        return tbl;
    }

    static const Base64Table &get_base64url_table() {
        static Base64Table tbl("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_","","");
        return tbl;
    }

};

template<typename Fn>
void base64encode(const std::string_view &binary, Fn &&output, const Base64Table &table = Base64Table::get_default_table()) {
    auto iter = binary.begin();
    auto iend = binary.end();

    while (iter != iend) {
        int b = static_cast<unsigned char>(*iter++);
        output(table.charset[b >> 2]);                 //bbbbbb
        if (iter != iend) {
            int c = static_cast<unsigned char>(*iter++);
            output(table.charset[((b<<4) | (c >> 4)) & 0x3F]);  //bbcccc
            if (iter != iend) {
                int d = static_cast<unsigned char>(*iter++);
                output(table.charset[((c<<2) | (d >> 6)) & 0x3F]); //ccccdd
                output(table.charset[d & 0x3F]);            //dddddd
            } else {
                output(table.charset[(c<<2) & 0x3F]);       //cccc00
                for (char c: table.trailer1) output(c);
            }
        } else {
            output(table.charset[(b<<4) & 0x3F]);           //bb0000
            for (char c: table.trailer2) output(c);
        }
    }
}


template<typename Fn>
void base64decode(const std::string_view &text, Fn &&output, const Base64Table &table = Base64Table::get_default_table()) {
    auto iter = text.begin();
    auto iend = text.end();
    int b=0,c=0,d=0,e=0;


    while  (iter != iend || (e & 0x80) != 0) {
        b = table.revtable[static_cast<unsigned char>(*iter++)];
        if (iter == iend || (b & 0x80) != 0) break;
        c = table.revtable[static_cast<unsigned char>(*iter++)];
        output(static_cast<char>((b<<2) | ((c & 0x3F) >> 4)));  //bbbbbbcc
        if (iter == iend || (c & 0x80) != 0 ) break;
        d = table.revtable[static_cast<unsigned char>(*iter++)];
        output(static_cast<char>(((c<<4) | ((d & 0x3F) >> 2))& 0xFF)); //ccccdddd
        if (iter == iend || (d & 0x80) != 0 ) break;
        e = table.revtable[static_cast<unsigned char>(*iter++)];
        output(static_cast<char>(((d<<6) | (e & 0x3F))& 0xFF));  //ddeeeeee
    }

}


}



#endif /* SRC_USERVER_BASE64_H_iowuqiowdqo8923yd23uhfd42 */
