/* Rates kept when the data subsystem left the registry. Both backends use
 * the same measurements: retiring an entry does not retire its work. The
 * native table generator reads these rows along with the live time tags. */
#ifndef CT_RETIRED_TIME_RATES_H
#define CT_RETIRED_TIME_RATES_H

#define CT_RETIRED_TIME_RATES(ENTRY) \
    ENTRY(0xC30002, unpack_stream, 4060) \
    ENTRY(0xC30557, unpack_stream_direct, 4057)

#endif
