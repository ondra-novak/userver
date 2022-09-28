#pragma once

#include <system_error>


namespace userver {

    /// Wrap the Win32 error code so we have a distinct type.
    struct win32_error_code
    {
        explicit win32_error_code(DWORD e) noexcept : error(e) {}
        DWORD error;
    };


    namespace detail
    {
        /// The Win32 error code category.
        class win32_error_category : public std::error_category
        {
        public:
            /// Return a short descriptive name for the category.
            char const* name() const noexcept override final { return "Win32Error"; }

            /// Return what each error code means in text.
            std::string message(int c) const override final
            {
                char error[256];
                auto len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL,
                    static_cast<DWORD>(c), 0, error, sizeof error,
                    NULL);
                if (len == 0)
                    return "N/A";
                // trim trailing newline
                while (len && (error[len - 1] == '\r' || error[len - 1] == '\n'))
                    --len;
                return std::string(error, len);
            }
        };
    }


    /// Return a static instance of the custom category.
    inline detail::win32_error_category const& win32_error_category()
    {
        static detail::win32_error_category c;
        return c;
    }

    // Overload the global make_error_code() free function with our
    // custom error. It will be found via ADL by the compiler if needed.
    inline std::error_code make_error_code(win32_error_code const& we)
    {
        return std::error_code(static_cast<int>(we.error), win32_error_category());
    }

    /// Create an error_code from a Windows error code.
    inline std::error_code make_win32_error_code(DWORD e)
    {
        return make_error_code(win32_error_code(e));
    }

    /// Create an error_code from the last Windows error.
    inline std::error_code make_win32_error_code()
    {
        return make_win32_error_code(GetLastError());
    }

    /// Create an error_code from the last Winsock error.
    inline std::error_code make_winsock_error_code()
    {
        return make_win32_error_code(WSAGetLastError());
    }

}

namespace std
{
    // Tell the C++ 11 STL metaprogramming that win32_error_code
    // is registered with the standard error code system.
    template <>
    struct is_error_code_enum<userver::win32_error_code> : std::true_type {};
}
