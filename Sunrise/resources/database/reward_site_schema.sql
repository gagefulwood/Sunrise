PRAGMA foreign_keys = ON;
-- Schema version 1 stores site identity and the first two supported operation families.
PRAGMA user_version = 1;

-- Catalog rows use nonzero unsigned 32-bit build identities.
-- Reward Site indices exclude the unsigned 16-bit absent-reference sentinel.
-- Provenance distinguishes recovered content from evidence-backed reconstruction.
CREATE TABLE reward_sites (
    image_timestamp INTEGER NOT NULL
        CHECK (image_timestamp >= 1 AND image_timestamp < (1 << 32)),
    image_size INTEGER NOT NULL CHECK (image_size >= 1 AND image_size < (1 << 32)),
    site_index INTEGER NOT NULL CHECK (site_index >= 0 AND site_index < ((1 << 16) - 1)),
    provenance TEXT NOT NULL CHECK (provenance IN ('recovered', 'reconstructed')),
    PRIMARY KEY (image_timestamp, image_size, site_index)
) STRICT;

-- Sunrise's 16-bit operation count supports zero-based ordinals 0 through 65534.
-- Installed item indices use the nonnegative half of the signed 16-bit domain.
-- Definition hashes are nonzero unsigned 32-bit values.
CREATE TABLE reward_site_item_progressions (
    image_timestamp INTEGER NOT NULL,
    image_size INTEGER NOT NULL,
    site_index INTEGER NOT NULL,
    ordinal INTEGER NOT NULL CHECK (ordinal >= 0 AND ordinal < ((1 << 16) - 1)),
    source_item_index INTEGER NOT NULL
        CHECK (source_item_index >= 0 AND source_item_index < (1 << 15)),
    source_item_hash INTEGER NOT NULL
        CHECK (source_item_hash >= 1 AND source_item_hash < (1 << 32)),
    successor_item_index INTEGER NOT NULL
        CHECK (successor_item_index >= 0 AND successor_item_index < (1 << 15)),
    successor_item_hash INTEGER NOT NULL
        CHECK (successor_item_hash >= 1 AND successor_item_hash < (1 << 32)),
    PRIMARY KEY (image_timestamp, image_size, site_index, ordinal),
    FOREIGN KEY (image_timestamp, image_size, site_index)
        REFERENCES reward_sites(image_timestamp, image_size, site_index)
) STRICT;

-- The selected-character object bank has 768 rows.
-- Sunrise's 16-bit operation count supports zero-based ordinals 0 through 65534.
-- Values use the full signed 32-bit state domain.
CREATE TABLE reward_site_character_object_transitions (
    image_timestamp INTEGER NOT NULL,
    image_size INTEGER NOT NULL,
    site_index INTEGER NOT NULL,
    ordinal INTEGER NOT NULL CHECK (ordinal >= 0 AND ordinal < ((1 << 16) - 1)),
    row_index INTEGER NOT NULL CHECK (row_index >= 0 AND row_index < 768),
    expected_value INTEGER NOT NULL
        CHECK (expected_value >= -(1 << 31) AND expected_value < (1 << 31)),
    next_value INTEGER NOT NULL CHECK (next_value >= -(1 << 31) AND next_value < (1 << 31)),
    PRIMARY KEY (image_timestamp, image_size, site_index, ordinal),
    FOREIGN KEY (image_timestamp, image_size, site_index)
        REFERENCES reward_sites(image_timestamp, image_size, site_index)
) STRICT;
