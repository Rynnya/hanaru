-- Disables "Table already exist" warnings
SET sql_notes = 0;

CREATE TABLE IF NOT EXISTS `beatmaps` (
    `id` int NOT NULL AUTO_INCREMENT,
    `beatmap_id` int NOT NULL DEFAULT '0',
    `beatmapset_id` int NOT NULL DEFAULT '0',
    `beatmap_md5` varchar(32) NOT NULL DEFAULT '',
    `artist` text NOT NULL,
    `title` text NOT NULL,
    `difficulty_name` longtext NOT NULL,
    `creator` text NOT NULL,
    `cs` float NOT NULL DEFAULT '0',
    `ar` float NOT NULL DEFAULT '0',
    `od` float NOT NULL DEFAULT '0',
    `hp` float NOT NULL DEFAULT '0',
    `mode` int NOT NULL DEFAULT '0',
    `rating` int NOT NULL DEFAULT '10',
    `difficulty_std` float NOT NULL DEFAULT '0',
    `difficulty_taiko` float NOT NULL DEFAULT '0',
    `difficulty_ctb` float NOT NULL DEFAULT '0',
    `difficulty_mania` float NOT NULL DEFAULT '0',
    `max_combo` int NOT NULL DEFAULT '0',
    `hit_length` int NOT NULL DEFAULT '0',
    `bpm` bigint NOT NULL DEFAULT '0',
    `count_normal` int NOT NULL DEFAULT '0',
    `count_slider` int NOT NULL DEFAULT '0',
    `count_spinner` int NOT NULL DEFAULT '0',
    `play_count` int NOT NULL DEFAULT '0',
    `pass_count` int NOT NULL DEFAULT '0',
    `ranked_status` tinyint NOT NULL DEFAULT '0',
    `latest_update` int NOT NULL DEFAULT '0',
    `ranked_status_freezed` tinyint NOT NULL DEFAULT '0',
    `creating_date` bigint NOT NULL DEFAULT '0',
    PRIMARY KEY (`id`),
    KEY `id` (`id`)
) ENGINE=InnoDB AUTO_INCREMENT=0 DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `beatmaps_names` (
    `id` int NOT NULL,
    `name` longtext NOT NULL
) ENGINE=InnoDB CHARSET=utf8;

-- Re-enables warning
SET sql_notes = 1;