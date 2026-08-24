import tempfile
import unittest
from pathlib import Path

from generate_map import map_metadata_source, write_map_metadata


class MapMetadataTest(unittest.TestCase):
    def test_source_contains_generated_coordinates(self) -> None:
        source = map_metadata_source(51.47, -0.4543)

        self.assertIn("CENTER_LATITUDE = 51.4700000f;", source)
        self.assertIn("CENTER_LONGITUDE = -0.4543000f;", source)
        self.assertIn("Do not edit manually", source)

    def test_write_creates_parent_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "assets" / "metadata.h"

            write_map_metadata(0.0, 0.0, output)

            self.assertEqual(
                output.read_text(encoding="utf-8"),
                map_metadata_source(0.0, 0.0),
            )


if __name__ == "__main__":
    unittest.main()
