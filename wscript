#
# Pebble app build rules.
#
import os
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')

    # DD_BOOT_COMBAT=1 boots straight into a fight, skipping the title and floor
    # menus. The emulator cannot send SELECT through the QEMU monitor and
    # `pebble emu-button` wedges screenshots for the rest of the session, so
    # without this the combat screen cannot be captured at all. Debug only.
    # DD_BOOT_FLOOR=3 starts a campaign run already standing on the boss floor.
    # The boss-win path runs through progress_mark_cleared, the achievement
    # unlocks and clear_run, and DD_BOOT_COMBAT cannot reach it because it
    # pushes combat with no node behind it. Independent of DD_BOOT_COMBAT.
    if os.environ.get('DD_BOOT_FLOOR'):
        for env in ctx.all_envs.values():
            env.append_value('DEFINES', ['DD_BOOT_FLOOR=' + os.environ['DD_BOOT_FLOOR'],
                                         'DD_BOOT_CLASS=' + os.environ.get('DD_BOOT_CLASS', '0'),
                                         'DD_BOOT_MODE=' + os.environ.get('DD_BOOT_MODE', '0')])

    # DD_ONESHOT gives one side 1 HP so a crash test reaches an end-of-fight
    # screen in one turn rather than sixteen rounds of blind button presses.
    # 1 = the hero one-shots anything and cannot die (the WIN path);
    # 2 = the hero dies to the first hit the enemy lands (the LOSE path).
    if os.environ.get('DD_HEAP'):
        for env in ctx.all_envs.values():
            env.append_value('DEFINES', ['DD_HEAP=1'])

    if os.environ.get('DD_ONESHOT'):
        for env in ctx.all_envs.values():
            env.append_value('DEFINES', ['DD_ONESHOT=' + os.environ['DD_ONESHOT']])

    if os.environ.get('DD_BOOT_COMBAT'):
        enemy = os.environ.get('DD_BOOT_ENEMY', '0')
        rule = os.environ.get('DD_BOOT_RULE', '0')
        for env in ctx.all_envs.values():
            defines = ['DD_BOOT_COMBAT=1',
                       'DD_BOOT_ENEMY=' + enemy,
                       'DD_BOOT_RULE=' + rule]
            # DD_BOOT_KIT=1 also fills every unlocked equipment slot, which is the
            # only way to exercise the scrolling grid: the cursor skips empty
            # slots, so with the default three-card kit there is nothing to
            # scroll to and the grid correctly refuses to move.
            if os.environ.get('DD_BOOT_KIT'):
                defines.append('DD_BOOT_KIT=1')
            defines.append('DD_BOOT_CLASS=' + os.environ.get('DD_BOOT_CLASS', '0'))
            env.append_value('DEFINES', defines)


def build(ctx):
    ctx.load('pebble_sdk')

    binaries = []
    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')
        binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries, js=[])
