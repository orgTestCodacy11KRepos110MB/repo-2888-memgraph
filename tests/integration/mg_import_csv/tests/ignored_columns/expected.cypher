CREATE INDEX ON :__mg_vertex__(__mg_id__);
CREATE (:__mg_vertex__:`Message`:`Comment` {__mg_id__: 0, `content`: "yes", `browser`: "Chrome", `country`: "Croatia"});
CREATE (:__mg_vertex__:`Message`:`Comment` {__mg_id__: 1, `content`: "thanks", `browser`: "Chrome", `country`: "United Kingdom"});
CREATE (:__mg_vertex__:`Message`:`Comment` {__mg_id__: 2, `content`: "LOL", `country`: "Germany"});
CREATE (:__mg_vertex__:`Message`:`Comment` {__mg_id__: 3, `content`: "I see", `browser`: "Firefox", `country`: "France"});
CREATE (:__mg_vertex__:`Message`:`Comment` {__mg_id__: 4, `content`: "fine", `browser`: "Internet Explorer", `country`: "Italy"});
MATCH (u:__mg_vertex__), (v:__mg_vertex__) WHERE u.__mg_id__ = 1 AND v.__mg_id__ = 2 CREATE (u)-[:`KNOWS` {`value`: 5}]->(v);
MATCH (u:__mg_vertex__), (v:__mg_vertex__) WHERE u.__mg_id__ = 4 AND v.__mg_id__ = 0 CREATE (u)-[:`KNOWS` {`value`: 6}]->(v);
DROP INDEX ON :__mg_vertex__(__mg_id__);
MATCH (u) REMOVE u:__mg_vertex__, u.__mg_id__;