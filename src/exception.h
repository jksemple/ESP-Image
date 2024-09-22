#ifndef __EXCEPTION_H
#define __EXCEPTION_H
#include "Arduino.h"
/**
 * Application error
 */
class Exception :  {
    public:
        /**
         * 
         */
        Error(const char* tag) : 
            _tag(tag), 
            _message(""),
            _isSevere(true) {
        }

        /**
         * Test if there's an error
         */
        operator bool() const {
            return !isOK();
        }

        /**
         * Test if there's an error
         */
        bool isOK() const {
            return _message == "";
        }

        /**
         * Test if error is severe
         */
        bool isSevere() const {
            return _isSevere && !isOK();
        }

        /**
         * Mark error as not severe
         */
        Error& soft() {
            _isSevere = false;

            return *this;
        }

        /**
         * Set exception message
         */
        Error& set(String error) {
            _message = error;
            _isSevere = true;

            return *this;
        }
        // Error& set(const char* error) {
        //     _message = String(error);
        //     _isSevere = true;

        //     return *this;
        // }
        Error& set(const char* format, ...) {
#define ERR_BUF_LEN 60
            char buffer[ERR_BUF_LEN];

            if (! format) {
                _message = "Missing error format string";
            } else {
                va_list args;
                va_start(args, format);
                vsnprintf(buffer, sizeof(buffer), format, args);
                _message = String(buffer);
            } 
            _isSevere = true;
            return *this;
        }

        /**
         * Clear exception
         */
        Error& clear() {
            return set("");
        }

        /**
         * 
         */
        template<typename Other>
        Error& propagate(Other& other) {
            other.set(this->toString());

            return *this;
        }

        /**
         * Convert exception to string
         */
        inline String toString() {
            return _message;
        }

        /**
         * Convert exception to char*
         */
        inline const char* toCString() {
            return toString().c_str();
        }

        /**
         * 
         */
        static Error none() {
            return Error("");
        }

    protected:
        const char* _tag;
        bool _isSevere;
        String _message;
};

#endif