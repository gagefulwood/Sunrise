-- Build 86657 PE identity; definitions never cross executable builds.
-- Site 11481 advances the first supported Unlimited Power stage.
INSERT INTO reward_sites (image_timestamp, image_size, site_index, provenance)
VALUES (1598231435, 145091072, 11481, 'reconstructed');

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
) VALUES (1598231435, 145091072, 11481, 0, 15284, 3398477426, 15285, 4280995080);

-- Character object row 526 advances from stage value 100 to 200.
INSERT INTO reward_site_character_object_transitions (
    image_timestamp,
    image_size,
    site_index,
    ordinal,
    row_index,
    expected_value,
    next_value
) VALUES (1598231435, 145091072, 11481, 0, 526, 100, 200);
