# pyright: reportMissingModuleSource=false
# pyright: reportMissingTypeStubs=false

from __future__ import annotations
from typing import TYPE_CHECKING, cast
from functools import cache
from pathlib import Path

from unrealsdk import (
    logging,
    load_package,
    find_enum,
    find_all,
    find_class,
    find_object,
)
from mods_base import build_mod, ObjectFlags

if TYPE_CHECKING:
    from BL1.Core import Object
    from BL1.WillowGame import (
        SkillDefinition,
        EEffectDurationType,
    )

    ENGINE: Object
else:
    from mods_base import ENGINE

    EEffectDurationType = find_enum("EEffectDurationType")


def load_script_package() -> None:
    # this takes an absolute path so you don't actually need to install it into the game directory
    #  you can store it next to your source files. Though if you package it into a .sdkmod you'll
    #  need to extract it; not a fan of the two-step install tbh.

    LOCAL_PACKAGE_FILE = Path(__file__).parent / "MayaPhaselock.u"

    if LOCAL_PACKAGE_FILE.is_file():
        pkg = load_package(LOCAL_PACKAGE_FILE.absolute().as_posix())
    else:
        pkg = load_package("MayaPhaselock")

    # oingo boingo try not to crash challenge
    for cls in find_all("Class"):
        if cls.Outer == pkg:
            cls.ObjectFlags |= ObjectFlags.KEEP_ALIVE

    logging.info("MayaPhaselock script package loaded")


@cache
def patch_phasewalk() -> SkillDefinition:
    skill = cast(
        "SkillDefinition",
        ENGINE.DynamicLoadObject(
            "gd_skills2_lilith.action.a_phasewalk",
            find_class("SkillDefinition"),
        ),
    )

    skill.SkillClass = find_class("MayaPhaselock.PhaselockSkill")
    skill.DurationType = EEffectDurationType.DURATION_Timed
    skill.SkillName = "Phaselock"
    skill.InitialDuration = 5.0
    skill.SkillDescription = "Grabs an enemy and suspends them in the air for a short period"

    skill.SkillEffectDefinitions.clear()
    skill.SkillActivationActions.clear()
    skill.SkillDeactivationActions.clear()
    skill.EventResponses.clear()

    skill.Behaviors.OnActivated.clear()
    skill.Behaviors.OnDeactivated.clear()
    skill.Behaviors.OnPaused.clear()
    skill.Behaviors.OnResumed.clear()
    skill.Behaviors.DamagedEvents.clear()
    skill.Behaviors.KilledEvents.clear()
    skill.Behaviors.OnSkillGradeIncreased.clear()

    skill.ObjectFlags |= ObjectFlags.KEEP_ALIVE

    return skill


def _on_enable() -> None:
    _ = patch_phasewalk()
    logging.info("Lilith skill patched to be Phaselock")


try:
    _ = find_object("Package", "MayaPhaselock")
except Exception:
    load_script_package()


_ = build_mod(on_enable=_on_enable)
