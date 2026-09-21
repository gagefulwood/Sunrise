-- Build 86657 PE identity; definitions never cross executable builds.
-- See reward_site_authoring.txt before adding a site or interpreting these reconstructed rows.
INSERT INTO reward_site_builds (image_timestamp, image_size)
VALUES (
    1598231435,   -- Build 86657 PE timestamp.
    145091072     -- Build 86657 image size.
);

-- Site 11481 advances the first supported Unlimited Power stage.
INSERT INTO reward_sites (image_timestamp, image_size, site_index, provenance)
VALUES (
    1598231435,   -- Build 86657 PE timestamp.
    145091072,    -- Build 86657 image size.
    11481,        -- Unlimited Power first-stage completion site.
    'reconstructed'
);

-- Installed item rows 15284 and 15285 are the source and successor quest stages.
INSERT INTO reward_site_item_progressions (
    image_timestamp,
    image_size,
    site_index,
    ordinal,
    source_item_index,
    source_item_hash,
    successor_item_index,
    successor_item_hash
) VALUES (
    1598231435,   -- Build 86657 PE timestamp.
    145091072,    -- Build 86657 image size.
    11481,        -- Owning Reward Site.
    0,            -- First item operation in this site.
    15284,        -- Unlimited Power source stage.
    3398477426,   -- Source-stage definition hash.
    15285,        -- Unlimited Power successor stage.
    4280995080    -- Successor-stage definition hash.
);

-- Character object row 526 advances from stage value 100 to 200.
INSERT INTO reward_site_character_object_transitions (
    image_timestamp,
    image_size,
    site_index,
    ordinal,
    row_index,
    expected_value,
    next_value
) VALUES (
    1598231435,   -- Build 86657 PE timestamp.
    145091072,    -- Build 86657 image size.
    11481,        -- Owning Reward Site.
    0,            -- First character-object operation in this site.
    526,          -- Unlimited Power character-object row.
    100,          -- First-stage value.
    200           -- Successor-stage value.
);
