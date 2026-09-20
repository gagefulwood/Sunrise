PRAGMA foreign_keys = ON;
-- Schema version 1 stores site identity and the first two supported operation families.
PRAGMA user_version = 1;

-- PE identity fields are unsigned 32-bit values; zero identifies no supported image.
-- Site index 65535 is the native absent-reference sentinel.
CREATE TABLE reward_sites (
    image_timestamp INTEGER NOT NULL CHECK (image_timestamp BETWEEN 1 AND 4294967295),
    image_size INTEGER NOT NULL CHECK (image_size BETWEEN 1 AND 4294967295),
    site_index INTEGER NOT NULL CHECK (site_index BETWEEN 0 AND 65534),
    provenance TEXT NOT NULL CHECK (provenance IN ('recovered', 'reconstructed')),
    PRIMARY KEY (image_timestamp, image_size, site_index)
) STRICT;

-- Installed item indices use the nonnegative half of the native signed 16-bit domain.
CREATE TABLE reward_site_item_progressions (
    image_timestamp INTEGER NOT NULL,
    image_size INTEGER NOT NULL,
    site_index INTEGER NOT NULL,
    ordinal INTEGER NOT NULL CHECK (ordinal BETWEEN 0 AND 65534),
    source_item_index INTEGER NOT NULL CHECK (source_item_index BETWEEN 0 AND 32767),
    source_item_hash INTEGER NOT NULL CHECK (source_item_hash BETWEEN 1 AND 4294967295),
    successor_item_index INTEGER NOT NULL CHECK (successor_item_index BETWEEN 0 AND 32767),
    successor_item_hash INTEGER NOT NULL CHECK (successor_item_hash BETWEEN 1 AND 4294967295),
    PRIMARY KEY (image_timestamp, image_size, site_index, ordinal),
    FOREIGN KEY (image_timestamp, image_size, site_index)
        REFERENCES reward_sites(image_timestamp, image_size, site_index)
) STRICT;

-- The selected-character object value bank contains rows 0 through 767.
-- Values use the full signed 32-bit state domain.
CREATE TABLE reward_site_character_object_transitions (
    image_timestamp INTEGER NOT NULL,
    image_size INTEGER NOT NULL,
    site_index INTEGER NOT NULL,
    ordinal INTEGER NOT NULL CHECK (ordinal BETWEEN 0 AND 65534),
    row_index INTEGER NOT NULL CHECK (row_index BETWEEN 0 AND 767),
    expected_value INTEGER NOT NULL CHECK (expected_value BETWEEN -2147483648 AND 2147483647),
    next_value INTEGER NOT NULL CHECK (next_value BETWEEN -2147483648 AND 2147483647),
    PRIMARY KEY (image_timestamp, image_size, site_index, ordinal),
    FOREIGN KEY (image_timestamp, image_size, site_index)
        REFERENCES reward_sites(image_timestamp, image_size, site_index)
) STRICT;
