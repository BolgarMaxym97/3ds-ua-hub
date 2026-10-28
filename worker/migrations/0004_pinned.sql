-- A translation kept at the top of the app's list, whatever the sort (the system translation, 3ds-ua).
ALTER TABLE translations ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0 CHECK (pinned IN (0, 1));
