#ifndef AVRCOMPAT_H
#define AVRCOMPAT_H

#include <stdlib.h> // rand() - used by arand() below
#include <string.h> // memcpy/memcmp/strcpy/strcmp/strlen - aliased below
#include <math.h>   // sin/cos/floor/etc - Vircon32's own math.h auto-provides
                    // these with no #include needed; some ported games
                    // (gameHollowSeeker.c, gameWrenRollercoaster.c) call them
                    // directly assuming that

// -----------------------------------------------------------------------------
// Small compatibility layer so upstream TinyJoypad game sources (written
// against AVR/Arduino headers this project never includes) can be ported
// with minimal line-by-line editing. Ported from the Vircon32 build's own
// avrCompat.h - see that file's own header comment for the full original
// rationale (Vircon32's only integer type is a 32-bit int, no separate
// flash address space, etc).
//
// IMPORTANT: uint8_t/int8_t/etc stay aliased to plain `int` here too, even
// though this is now a real desktop C compiler with real 1-byte types
// available - this is deliberate, not an oversight. Dozens of bugs across
// this whole porting project's history (see tinyjoypad_vircon32/CLAUDE.md)
// were found and fixed on the assumption that these types NEVER silently
// truncate/narrow - restoring real 1-byte uint8_t here would risk
// reintroducing every one of those bugs into code that was written/
// debugged assuming 32-bit-int semantics throughout. Do not "fix" this.
//
// `#define`d, not `typedef`'d (unlike the Vircon32 build's own version of
// this file, which can typedef freely since Vircon32 has no competing
// libc): on Apple's SDK, <stdlib.h> just above transitively needs the
// REAL uint8_t/int8_t/uint16_t/int16_t/uint32_t (unlike MinGW/glibc,
// found via a real macOS CI run) - deep system headers like
// sys/resource.h declare their own struct fields with them
// (`uint8_t ri_uuid[16];`), and <stdint.h> itself defines
// `uint_least32_t` etc. in terms of them, so those real typedefs MUST be
// allowed to stand. A `typedef int uint8_t;` here would conflict with
// that real typedef (C can't redefine a typedef to a different type) -
// tried first, and broke exactly that (`error: typedef redefinition with
// different types`). Pre-defining the real headers' own include guards to
// suppress them instead (a second attempt) broke worse: those same deep
// system headers still need the *real* narrow types for their own
// internal fields and no longer had them (`error: unknown type name
// 'uint8_t'`). A `#define` sidesteps both failure modes - it doesn't
// redeclare the real `uint8_t` typedef at all, it just rewrites the raw
// *token* `uint8_t` to `int` for every line of source from this point
// forward in this translation unit (the rest of this file, then
// machineDependent.h/the shim headers/the actual game source) - by which
// point <stdlib.h>/<string.h>/<math.h> have already been fully processed
// using the real types, so nothing downstream loses them. Confirmed
// nothing later in this translation unit needs the real names back
// (machineDependent.h only adds <stdbool.h>/<stddef.h>, neither of which
// touches these types) - see itoa's own near-identical `#define` redirect
// near the bottom of this file for the same technique used for the same
// class of reason (MinGW's own non-static libc `itoa()`, there).
//
// Two real differences from the Vircon32 version:
// - `size_t` is NOT aliased here (confirmed via grep: unused anywhere else
//   in the whole project) - the real system size_t (via <stddef.h>,
//   already pulled in by machineDependent.h) is used instead. Aliasing it
//   to `int` here would hard-conflict with the real one the instant any
//   standard library header needing it is in the same translation unit
//   (memcpy/strlen/etc all do) - this is exactly why the "game world" and
//   "SDL platform backend" are kept as separate translation units at all
//   (see machineDependent.h's own note).
// - `max`/`min` are added below: Vircon32's own misc.h/math.h provide
//   these as built-ins and the ported game code calls them as if
//   standard, but standard C has no such functions.
// -----------------------------------------------------------------------------

#define uint8_t  int
#define int8_t   int
#define uint16_t int
#define int16_t  int
#define uint32_t int
#define int32_t  int

#define PROGMEM

#define pgm_read_byte(addr)  (*(addr))
#define pgm_read_word(addr)  (*(addr))
#define pgm_read_dword(addr) (*(addr))

#define memcpy_P memcpy
#define memcmp_P memcmp
#define strcpy_P strcpy
#define strcmp_P strcmp
#define strlen_P strlen

// Arduino's "flash string" helper - no separate flash address space here
// either, so a flash string is just a normal int[] string literal.
#define __FlashStringHelper int
#define F(s) s

#define bit(n)            ( 1 << (n) )
#define bitRead(value, n) ( ( (value) >> (n) ) & 1 )
#define bitSet(value, n)  ( (value) |= ( 1 << (n) ) )
#define bitClear(value, n) ( (value) &= ~( 1 << (n) ) )

// Vircon32's own misc.h/math.h provide these as real builtins; standard C
// doesn't, and the ported game code already calls them assuming it does.
#define max(a, b) ( (a) > (b) ? (a) : (b) )
#define min(a, b) ( (a) < (b) ? (a) : (b) )

// Vircon32's own math.h defines this as a real built-in constant (same
// value, same name) - some ported games (gameHollowSeeker.c) use it
// directly assuming it's already available.
#define pi 3.1415926

// AVR's rand() (Arduino/avr-libc) returns a 15-bit non-negative value
// (0..32767). This helper exists in the Vircon32 build because Vircon32's
// own rand() instead returns the raw 32-bit RNG register directly (any
// sign, any magnitude), which silently broke code doing `rand() % n`.
// Standard C's rand() is already non-negative (0..RAND_MAX), so this
// helper isn't strictly load-bearing here the way it was there - kept
// anyway (unchanged signature/behavior) since every game already calls
// it, and it's harmless/correct against a well-behaved rand() too.
//
// `static`, here and on itoa() below: this header is now #include'd by
// every games/*.c file as its own separate translation unit (each game
// compiled on its own, not stitched together into one TU anymore - see
// CLAUDE.md's "Translation-unit boundary" section) - without `static`,
// every one of those TUs would emit an external definition of the same
// function name, and the link would fail with "multiple definition of
// arand"/"itoa". `static` gives each TU its own private copy instead,
// which is exactly what a tiny header-only helper like this wants anyway.
static int arand( int n )
{
    if( n <= 0 )
      return 0;

    int r = rand();
    if( r < 0 )
      r = -r;

    return r % n;
}

// Not standard C (an MSVC/mingw extension, absent on glibc) - menu.c uses
// it for page-number formatting. Small, portable replacement: converts a
// non-negative int to decimal digits in buf (base parameter kept for
// call-site compatibility but only base 10 is actually implemented, since
// nothing in this project ever calls it with anything else).
//
// Named avrCompatItoa, not itoa, and routed to that name via the #define
// below: MinGW's own <stdlib.h> (pulled in above) already declares a
// non-static itoa() of its own (a deprecated MSVC CRT extension) - `static`
// (needed here for the same multi-TU reason as arand() above) can't
// re-declare a name stdlib.h already declared non-static in the same TU
// ("static declaration follows non-static declaration"). Routing every
// call site through a macro means the compiler never sees two conflicting
// declarations of the identifier `itoa` at all.
static char* avrCompatItoa( int value, char* buf, int base )
{
    if( base != 10 || value == 0 )
    {
        buf[ 0 ] = '0';
        buf[ 1 ] = 0;
        return buf;
    }

    int isNegative = ( value < 0 );
    if( isNegative )
      value = -value;

    char tmp[ 12 ];
    int n = 0;
    while( value > 0 )
    {
        tmp[ n ] = '0' + ( value % 10 );
        value /= 10;
        n++;
    }

    int i = 0;
    if( isNegative )
      buf[ i++ ] = '-';
    while( n > 0 )
      buf[ i++ ] = tmp[ --n ];
    buf[ i ] = 0;

    return buf;
}

#define itoa avrCompatItoa

#endif
