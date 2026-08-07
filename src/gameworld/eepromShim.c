#include "avrCompat.h"
#include "machineDependent.h"
#include "eepromShim.h"
#include <string.h>

// =============================================================================
// eepromShim.c - see eepromShim.h for the public API and its own rationale.
// =============================================================================
// On-file layout: byte 0 - EEPROM_SIGNATURE_BYTES-1 is md_cardWriteSignature()'s
// own fixed signature (identifies "this file was written by this project" as
// a whole - see each port's own md_card*() implementation). Byte
// EEPROM_SIGNATURE_BYTES onward is a flat EEPROM_MAX_SLOTS-length table of
// EepromSlot, slot N at byte offset EEPROM_SIGNATURE_BYTES + N*EEPROM_SLOT_BYTES.
//
// **Looked up by name, not registration index** - a game's own slot is
// found (or claimed, the first time it's ever selected) via an open-
// addressing hash table: eepromHashTitle(title) % EEPROM_MAX_SLOTS picks a
// starting probe slot, then eepromSelectGame() scans forward (wrapping)
// until it finds either a slot whose stored nameTag already matches this
// exact title (reuse it) or a genuinely empty slot (claim it). This means a
// game's own save data always lands wherever its *name* hashes to,
// regardless of where it sits in menuGameList.c's own addGame() call order -
// reordering the menu, or inserting a new game between two existing ones,
// can never silently swap or corrupt an unrelated game's own save. Open
// addressing guarantees zero collisions between any two distinct titles as
// long as the table isn't completely full - trivially true here
// (EEPROM_MAX_SLOTS=64, well under this project's own current ~50
// registered games, most of which don't even use this shim).
//
// EEPROM_MAX_SLOTS is deliberately its own independent constant, not tied
// to menu.c's own MAX_GAMES - each games/*.c and menu.c is its own separate
// translation unit now (see CLAUDE.md's "Translation-unit boundary"
// section), unlike the Vircon32 build's own single-TU-via-#include shape
// (where eepromShim.c could see menu.c's MAX_GAMES directly). The two
// values never need to match exactly - this one only needs to stay
// comfortably >= however many real games actually call eepromSelectGame().
//
// **Fresh/never-written cells default to 255 (0xFF), not 0** - matching
// real AVR EEPROM's own actual factory-erased state, not an arbitrary zero.
// This matters: several upstream games explicitly check for a literal 255
// as their own "this EEPROM has never been written" sentinel (e.g.
// attiny85-flappy-bird's own `if (high_score == 255) high_score = 0;`,
// ATtiny Tetromino's own `if (top == 255) ...`) - defaulting to 0 instead
// would have silently changed what those checks actually detect.
// =============================================================================

// Sized 24 chars at the time this shim was first built - comfortably over
// the longest real menu title then (18 characters, "WREN ROLLERCOASTER").
// Bumped to 32 (8 chars of real margin over the new longest title) when
// "GILBERT IN THE DOWNLAND" (23 characters) shipped - ported directly from
// the sibling tinyjoypad_vircon32 build's own identical proactive fix
// there (its own EEPROM_TAG_WORDS 24->32), made after a direct user
// question caught that title landing with genuinely zero spare capacity
// in ITS OWN 24-word buffer there (an unbounded `strcpy()`, one character
// away from overflowing into the next struct field). This project's own
// `eepromResetCurrentSlotToFresh()` already uses a bounded `strncpy()`
// (see that function's own comment) so a too-long title here would only
// ever truncate, never overflow into `magic`/`checksum` - but the same
// margin-widening is still worth mirroring, since a truncated tag would
// silently break that specific game's own save-matching (the stored,
// truncated tag would never again equal the real full title on a future
// lookup) rather than corrupting anything. Changes the on-disk slot size
// (EEPROM_SLOT_BYTES derives from sizeof(EepromSlot)), so any high scores
// already saved under the old 24-char layout land at different offsets
// under this one and read back as "no matching slot" (a fresh, zeroed
// high score) rather than being corrupted - an acceptable, one-time reset
// this early in the project's own life, not something worth writing
// migration code for.
#define EEPROM_TAG_CHARS 32
#define EEPROM_SLOT_DATA_SIZE 512
#define EEPROM_MAGIC 0x45455032
#define EEPROM_MAX_SLOTS 64
#define EEPROM_SIGNATURE_BYTES 32

typedef struct
{
    char nameTag[ EEPROM_TAG_CHARS ];
    int magic;
    int checksum;
    // Stored as real bytes (unsigned char), not one int per cell the way
    // the Vircon32 build's own version does - that file widens every cell
    // to a full Vircon32 int specifically because avrCompat.h widens every
    // uint8_t project-wide to avoid truncation bugs mid-*computation*; this
    // struct is purely an on-disk *serialization* format, a different
    // concern entirely. eeprom_read_byte()/eeprom_write_byte() below still
    // take/return a plain int at their own call sites (matching every
    // already-ported game's own call sites, unchanged), just narrowed to a
    // real byte's worth of value here - matching what real AVR EEPROM
    // storage itself actually looks like, and quartering the on-disk file
    // size in the process.
    unsigned char data[ EEPROM_SLOT_DATA_SIZE ];
} EepromSlot;

#define EEPROM_SLOT_BYTES ( (int)sizeof( EepromSlot ) )

static EepromSlot currentSlot;
static int currentSlotOffset;
static bool eepromCardAvailable;

static int eepromHashTitle( char* title )
{
    int hash = 0;
    for( int i = 0; title[ i ] != 0; i++ )
      hash = hash * 31 + (unsigned char)title[ i ];
    if( hash < 0 ) hash = -hash;
    return hash;
}

static int eepromCalcChecksum()
{
    int sum = 0;
    for( int i = 0; i < EEPROM_SLOT_DATA_SIZE; i++ )
      sum += currentSlot.data[ i ];
    return sum;
}

static void eepromResetCurrentSlotToFresh( char* title )
{
    memset( currentSlot.nameTag, 0, EEPROM_TAG_CHARS );
    strncpy( currentSlot.nameTag, title, EEPROM_TAG_CHARS - 1 );
    currentSlot.magic = EEPROM_MAGIC;
    memset( currentSlot.data, 255, EEPROM_SLOT_DATA_SIZE );
    currentSlot.checksum = eepromCalcChecksum();
}

void eepromSelectGame( char* title )
{
    eepromResetCurrentSlotToFresh( title );
    eepromCardAvailable = false;
    currentSlotOffset = -1;

    if( !md_cardIsConnected() ) return;

    // Stamp a fresh/foreign file once, the first time anything ever tries
    // to use it - every slot past the signature on a freshly-stamped file
    // reads back as real zeroed storage, which is not EEPROM_MAGIC, so the
    // "is this slot empty" check below already handles it correctly with
    // no special-casing needed here.
    if( !md_cardHasOurSignature() ) md_cardWriteSignature();

    int startSlot = eepromHashTitle( title ) % EEPROM_MAX_SLOTS;
    for( int probe = 0; probe < EEPROM_MAX_SLOTS; probe++ )
    {
        int slotIndex = ( startSlot + probe ) % EEPROM_MAX_SLOTS;
        int slotOffset = EEPROM_SIGNATURE_BYTES + slotIndex * EEPROM_SLOT_BYTES;

        EepromSlot candidate;
        md_cardReadData( &candidate, slotOffset, EEPROM_SLOT_BYTES );

        bool isEmpty = ( candidate.magic != EEPROM_MAGIC );

        if( !isEmpty && strncmp( candidate.nameTag, title, EEPROM_TAG_CHARS ) == 0 )
        {
            currentSlot = candidate;
            currentSlotOffset = slotOffset;
            eepromCardAvailable = true;

            if( currentSlot.checksum != eepromCalcChecksum() )
            {
                // corrupted or torn write - start this game fresh rather
                // than trust garbage data
                eepromResetCurrentSlotToFresh( title );
                md_cardWriteData( &currentSlot, slotOffset, EEPROM_SLOT_BYTES );
            }
            return;
        }

        if( isEmpty )
        {
            eepromResetCurrentSlotToFresh( title );
            currentSlotOffset = slotOffset;
            eepromCardAvailable = true;
            md_cardWriteData( &currentSlot, slotOffset, EEPROM_SLOT_BYTES );
            return;
        }

        // occupied by a different game's own tag - keep probing
    }

    // table is completely full (should never happen at EEPROM_MAX_SLOTS=64
    // with well under 64 real games using this shim) - fall back to no
    // persistence this session rather than loop forever; eepromCardAvailable
    // stays false, so every write below silently no-ops.
}

int eeprom_read_byte( int address )
{
    if( address < 0 || address >= EEPROM_SLOT_DATA_SIZE ) return 255;
    return currentSlot.data[ address ];
}

// Writes the whole slot back to disk on every call, not just the changed
// byte/checksum the way the Vircon32 build's own version does - that
// partial-write optimization existed there to minimize real memory-card
// I/O; here it's one local ~570-byte file write, cheap enough that the
// simpler "always write the whole slot" version isn't worth the extra
// offset-math surface area (and this only ever fires on a genuine new high
// score, never per-frame).
void eeprom_write_byte( int address, int value )
{
    if( !eepromCardAvailable ) return;
    if( address < 0 || address >= EEPROM_SLOT_DATA_SIZE ) return;

    currentSlot.data[ address ] = (unsigned char)( value & 0xFF );
    currentSlot.checksum = eepromCalcChecksum();

    md_cardWriteData( &currentSlot, currentSlotOffset, EEPROM_SLOT_BYTES );
}

void eeprom_update_byte( int address, int value )
{
    if( address < 0 || address >= EEPROM_SLOT_DATA_SIZE ) return;
    if( currentSlot.data[ address ] == (unsigned char)( value & 0xFF ) ) return;
    eeprom_write_byte( address, value );
}

// Treated as two/four consecutive byte cells (matching how eeprom_read_block()
// below treats any run of addresses) - each real upstream call site already
// does its own hi/lo combining, so this shim doesn't need to guess or
// preserve any particular endianness convention.
int eeprom_read_word( int address )
{
    int hi = eeprom_read_byte( address );
    int lo = eeprom_read_byte( address + 1 );
    return ( hi << 8 ) | ( lo & 0xFF );
}

void eeprom_write_word( int address, int value )
{
    eeprom_write_byte( address, ( value >> 8 ) & 0xFF );
    eeprom_write_byte( address + 1, value & 0xFF );
}

int eeprom_read_dword( int address )
{
    int b0 = eeprom_read_byte( address );
    int b1 = eeprom_read_byte( address + 1 );
    int b2 = eeprom_read_byte( address + 2 );
    int b3 = eeprom_read_byte( address + 3 );
    return ( b0 << 24 ) | ( ( b1 & 0xFF ) << 16 ) | ( ( b2 & 0xFF ) << 8 ) | ( b3 & 0xFF );
}

void eeprom_write_dword( int address, int value )
{
    eeprom_write_byte( address, ( value >> 24 ) & 0xFF );
    eeprom_write_byte( address + 1, ( value >> 16 ) & 0xFF );
    eeprom_write_byte( address + 2, ( value >> 8 ) & 0xFF );
    eeprom_write_byte( address + 3, value & 0xFF );
}

void eeprom_read_block( void* dest, int address, int size )
{
    int* destInts = (int*)dest;
    for( int i = 0; i < size; i++ )
      destInts[ i ] = eeprom_read_byte( address + i );
}

void eeprom_write_block( void* src, int address, int size )
{
    int* srcInts = (int*)src;
    for( int i = 0; i < size; i++ )
      eeprom_write_byte( address + i, srcInts[ i ] );
}

void eeprom_busy_wait()
{
    // no-op - real file I/O has no write latency to wait out
}
