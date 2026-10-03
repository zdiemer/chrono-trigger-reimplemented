# How the native mixer was built

What making the native mixer (docs/PLATFORM.md, "The native mixer, as built")
taught, kept here rather than in the milestone doc, which says what the mixer
is and how it is checked now.

## Why the sample directory is in sound memory

The first version kept the sample directory as a table of its own, since the
game never moves the directory's page. The frame-for-frame comparison with
the S-DSP parted where an echo buffer whose delay had changed ran on at its
old size from its new place, past the top of sound memory and round into the
bottom, reading the directory there into the echo. So the directory moved into
the mixer's sound memory, where the original driver keeps it, and the whole
layout became the original's: where a sample or the directory lies decides
what the echo reads and overwrites.

## What the random comparison was shown to catch

`dsp_test`'s random comparison (random calls of every kind the driver makes
over random BRR samples, through the S-DSP's registers and the mixer at once)
was checked by changing the mixer by hand: started a note in four frames
rather than five, released it a step slower, or switched the echo's right
write a frame late, it failed each time. The same exercise showed what it
couldn't see: none of its random samples made the Gaussian interpolation's sum
wrap, so a mixer without the wrap passed it. A hand-built block of the most
negative samples, played where the three weights summed before the wrap come
to more than 2048, now reaches it (`test_gaussian_wrap`), and fails without
the wrap.
