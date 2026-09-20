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
