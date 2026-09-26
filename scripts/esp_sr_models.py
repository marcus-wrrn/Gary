# PlatformIO pre-script: packs the ESP-SR models selected in menuconfig into
# srmodels.bin and flashes it to the "model" partition alongside the app.
# ESP-SR only wires this into `idf.py flash`, which PlatformIO doesn't use.
import csv
import os

Import("env")

PROJECT_DIR = env.subst("$PROJECT_DIR")
BUILD_DIR = env.subst("$BUILD_DIR")
MODEL_PARTITION = "model"

board = env.BoardConfig()
sdkconfig_path = board.get(
    "build.esp-idf.sdkconfig_path",
    os.path.join(PROJECT_DIR, "sdkconfig.%s" % env.subst("$PIOENV")),
)
partitions_path = os.path.join(PROJECT_DIR, board.get("build.partitions", "partitions.csv"))
esp_sr_dir = os.path.join(PROJECT_DIR, "managed_components", "espressif__esp-sr")
srmodels_bin = os.path.join(BUILD_DIR, "srmodels", "srmodels.bin")


def parse_size(value):
    value = value.strip().upper()
    if value.endswith("K"):
        return int(value[:-1], 0) * 1024
    if value.endswith("M"):
        return int(value[:-1], 0) * 1024 * 1024
    return int(value, 0)


def find_model_partition():
    with open(partitions_path, newline="") as f:
        rows = (row for row in csv.reader(f) if row and not row[0].strip().startswith("#"))
        for row in rows:
            if row[0].strip() == MODEL_PARTITION:
                offset, size = row[3].strip(), row[4].strip()
                if not offset:
                    env.Exit("esp_sr_models: give the '%s' partition an explicit offset in %s"
                             % (MODEL_PARTITION, partitions_path))
                return offset, parse_size(size)
    env.Exit("esp_sr_models: no '%s' partition in %s" % (MODEL_PARTITION, partitions_path))


def pack_models(target, source, env):
    # managed_components/ is populated by the component manager during configure,
    # so it only exists once the framework has run at least once
    if not os.path.isdir(esp_sr_dir):
        print("esp_sr_models: %s not found, has the component manager run?" % esp_sr_dir)
        return 1

    result = env.Execute('"$PYTHONEXE" "%s" -d1 "%s" -d2 "%s" -d3 "%s"' % (
        os.path.join(esp_sr_dir, "model", "movemodel.py"), sdkconfig_path, esp_sr_dir, BUILD_DIR))
    if result:
        return result

    packed = os.path.getsize(srmodels_bin)
    if packed > model_size:
        print("esp_sr_models: srmodels.bin is %d bytes but the '%s' partition is only %d"
              % (packed, MODEL_PARTITION, model_size))
        return 1
    return 0


model_offset, model_size = find_model_partition()

# Repacks whenever the sdkconfig (and so the model selection) changes
srmodels = env.Command(srmodels_bin, sdkconfig_path,
                       env.VerboseAction(pack_models, "Packing ESP-SR models into $TARGET"))
# Hooked onto the targets by name: PROGNAME is still "program" when pre-scripts
# run, so the firmware.bin path can't be named here. The platform's later
# Alias() calls for these names merge into these.
env.Depends(env.Alias("buildprog"), srmodels)
env.Depends(env.Alias("upload"), srmodels)

# The framework script appends the bootloader and partition table to this list
# after this runs, and the upload command is built from it after that
env.Append(FLASH_EXTRA_IMAGES=[(model_offset, srmodels_bin)])
