"""The programs under test and their refusal variants, and the sibling drivers.

Split out of native-owned-global-maps.py on 2026-09-08 (it was 1,013 lines); the
definitions are verbatim, only the two sibling-file paths changed, because this
module lives one directory below them.
"""

from .sources_scalar_maps import *
from .sources_nullable_maps import *
from .sources_object_maps import *
from .sources_globals import *
from .sources_map_keys import *
