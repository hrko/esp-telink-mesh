from pathlib import Path
import esphome.codegen as cg
import esphome.config_validation as cv

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    source = Path(__file__).resolve().parents[4].as_uri()
    cg.add_library("esp-telink-mesh", None, source)
