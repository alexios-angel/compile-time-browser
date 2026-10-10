"""The programs under test and their refusal variants, and the sibling drivers.

Split out of native-owned-global-maps.py on 2026-09-08 (it was 1,013 lines); the
definitions are verbatim, only the two sibling-file paths changed, because this
module lives one directory below them.
"""

from .scalar.maps import *
from .nullable_maps import *
from .object_maps import *
from .globals import *
from .map.keys import *

# A STAR IMPORT OF THIS PACKAGE exports what its modules define and import, as
# the module this package was did - not the submodules themselves, which as
# attributes of a package would otherwise go along and shadow names such as
# `globals` in every module that star-imports it.
import types as _types

__all__ = [
    _name
    for _name, _value in dict(vars()).items()
    if not _name.startswith("_")
    and not (isinstance(_value, _types.ModuleType) and _value.__name__.startswith(__name__ + "."))
]
