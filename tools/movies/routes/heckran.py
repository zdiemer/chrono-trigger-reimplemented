"""Beat Heckran from the cave save point, waiting out its water stance.

The cartridge save was made in normal play by heckran_prepare, from
ctsave03 slot 3. Story $4E is below the boss script's $51 threshold.
"""
from .. import policies
from ..campaign import Run, route_arguments, run_route

SAVE = "oracle/saves/routes/pre-heckran.srm"
PHASES = ["loaded", "battle", "won", "after"]
HEADER = """\
# Heckran's Cave, 1000 A.D.: load the cave save point, defeat Heckran with magic,
# wait out its water counterattack stance, and end with the field back in hand (route heckran).
# Save: pre-heckran slot 3 (oracle/saves/routes; normal play from ctsave03 slot 3 via heckran_prepare).
# frames {FRAMES}
# sram crc32 {CRC}
# <start_frame> <frame_count> <buttons>"""


def magic(bot, state, rng, inventory):
    for slot in range(3):
        if not state.hp(slot):
            action = policies.use(inventory, policies.REVIVE, slot)
            if action:
                return action
    low = min(range(3), key=lambda s: policies.fraction(state, s))
    if policies.fraction(state, low) < 0.4:
        for item in (policies.MID_TONIC, policies.TONIC):
            action = policies.use(inventory, item, low)
            if action:
                return action
    if state.mp(state.member) < 2:
        for item in (policies.ETHER, policies.MID_ETHER):
            action = policies.use(inventory, item, state.member)
            if action:
                return action
    living = policies.enemies(state)
    return ("tech", 2, min(living, key=lambda s: state.hp(s)) if living else None)


def fight(run, seed, boss=False):
    seen = []
    waited = []

    def won(r, state):
        living = policies.enemies(state)
        if living:
            seen.append(True)
        return bool(seen) and not living and any(state.hp(s) for s in range(3))

    def guard(bot):
        original = bot.do
        waiting = False

        def do(action):
            nonlocal waiting
            # Heckran is enemy slot 3: flags $04 raises its script counter,
            # flags $40 clears it after four waits. Guard even the runner's
            # fallback attack after failed menu choices.
            if bot.machine.b(0x7EB320):
                if not waiting:
                    run.say("water stance: wait", bot.machine.frame)
                    waited.append(bot.machine.frame)
                waiting = True
                bot.step(30)
                return True
            if waiting:
                run.say("water stance ended", bot.machine.frame)
            waiting = False
            return original(action)
        bot.do = do

    run.fight(magic, won, seed=seed, max_frames=28000, max_tries=100,
              setup=guard if boss else None)
    if boss:
        assert waited, "Heckran's water stance was waited out"
    assert run.wait_control(10000), "the field after the fight"


def main():
    args = route_arguments(__doc__.splitlines()[0], save=SAVE)
    run = Run(args.prefix, rom=args.rom, sram=args.sram, drive=args.drive,
              native=args.native, generated=args.generated, sound=args.sound_driver, until=args.until)
    m = run.machine
    try:
        start = run.start_at(PHASES)
        if start <= 0:
            m.run(1620)
            m.run(5, "START")
            m.run(2420 - m.frame)
            assert run.select_slot(2)
            m.run(4, "A")
            m.run(300)
            assert run.wait_control()
            assert run.map_id() == "3100" and m.b(0x7F0000) < 0x51
            run.checkpoint("loaded")
        if start <= 1:
            assert run.exit_to("2f00"), "Heckran's chamber"
            assert run.wait_control()
            assert run.walk(lambda machine, x, y: y < 32 * 16) is not None
            assert run.dialogs_until(lambda: m.b(0x7E95D5) < 0x80, 6000)
            run.checkpoint("battle")
        if start <= 2:
            fight(run, args.seed, boss=True)
            run.checkpoint("won")
        if start <= 3:
            assert run.find_player() is not None, "the field back in hand"
            assert run.map_id() == "2f00" and m.b(0x7F0000) == 0x51
            run.checkpoint("after")
        m.screenshot(run.prefix + "_end.png")
        run.finish("Heckran beaten and the field back in hand")
        if args.movie:
            run.write_movie(args.movie, HEADER.splitlines())
    finally:
        if m.process.poll() is None:
            run.close()


if __name__ == "__main__":
    raise SystemExit(run_route(main))
