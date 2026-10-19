/**
 * Test fixtures. People, connections, tags, questions, answers and badges are real rows from
 * data/stackoverflow (Stack Exchange Data Dump, CC BY-SA 4.0, attribution: Stack Exchange, Inc.
 * and the Stack Overflow contributors), trimmed to the fields the API returns. The signed-in
 * member ("Ada Member") and analytics numbers are made up for the tests.
 */
import type {
  AnalyticsSummary,
  AuthResponse,
  CommunitiesResponse,
  Connection,
  GraphResponse,
  InfluenceMetric,
  InfluencersResponse,
  PathResponse,
  QuestionDetail,
  QuestionSummary,
  Recommendation,
  Tag,
  Taxonomy,
  UserActivity,
  UserMetrics,
  UserProfile,
  UserProfileWithCount,
  UserRef,
} from '../api/types';

export const taxonomy: Taxonomy = {
  categories: [
    'Web Development',
    '.NET & Windows',
    'Java & JVM',
    'Systems Programming',
    'Scripting Languages',
    'Databases & Data',
    'Mobile & Apple',
    'DevOps & Tooling',
    'Computer Science',
    'Security & Networking',
  ],
  branches: [
    'javascript',
    'php',
    'css',
    'c#',
    '.net',
    'vb.net',
    'java',
    'c++',
    'c',
    'python',
    'ruby',
    'sql-server',
    'mysql',
    'iphone',
    'git',
    'algorithm',
    'security',
  ],
  branch_categories: {
    javascript: 'Web Development',
    php: 'Web Development',
    css: 'Web Development',
    'c#': '.NET & Windows',
    '.net': '.NET & Windows',
    'vb.net': '.NET & Windows',
    java: 'Java & JVM',
    'c++': 'Systems Programming',
    c: 'Systems Programming',
    python: 'Scripting Languages',
    ruby: 'Scripting Languages',
    'sql-server': 'Databases & Data',
    mysql: 'Databases & Data',
    iphone: 'Mobile & Apple',
    git: 'DevOps & Tooling',
    algorithm: 'Computer Science',
    security: 'Security & Networking',
  },
};

type ProfileInput = Pick<
  UserProfileWithCount,
  'id' | 'handle' | 'name' | 'category' | 'branch' | 'reputation' | 'influence' | 'created_at'
> &
  Partial<UserProfileWithCount>;

function imported(input: ProfileInput): UserProfileWithCount {
  return {
    location: null,
    website_url: null,
    about: null,
    views: 0,
    up_votes: 0,
    down_votes: 0,
    source: 'stackoverflow',
    external_url: `https://stackoverflow.com/users/${String(input.id)}`,
    last_access_at: '2024-04-06T18:17:34Z',
    connection_count: 0,
    ...input,
  };
}

export const me: UserProfileWithCount = {
  id: 100_000_123,
  handle: 'ada_member',
  name: 'Ada Member',
  category: 'Web Development',
  branch: 'javascript',
  influence: 0,
  reputation: 1,
  location: 'Lisbon',
  website_url: null,
  about: null,
  views: 0,
  up_votes: 0,
  down_votes: 0,
  source: 'member',
  external_url: null,
  created_at: '2026-10-19T09:00:00Z',
  last_access_at: '2026-10-19T09:00:00Z',
  connection_count: 2,
};

export const jonSkeet = imported({
  id: 22656,
  handle: 'jon_skeet_22656',
  name: 'Jon Skeet',
  category: '.NET & Windows',
  branch: 'c#',
  reputation: 1_457_162,
  influence: 100,
  created_at: '2008-09-26T12:05:05Z',
  location: 'Reading, United Kingdom',
  website_url: 'http://csharpindepth.com',
  about:
    '<p>Author of <a href="http://csharpindepth.com">C# in Depth</a></p><p>Currently a software engineer at Google, working remotely</p>',
  views: 2_294_496,
  up_votes: 17_086,
  down_votes: 7_966,
  connection_count: 1_207,
});

export const marcGravell = imported({
  id: 23354,
  handle: 'marc_gravell_23354',
  name: 'Marc Gravell',
  category: '.NET & Windows',
  branch: 'c#',
  reputation: 1_043_814,
  influence: 99.99,
  created_at: '2008-09-29T05:46:02Z',
  location: 'Forest of Dean, United Kingdom',
  website_url: 'http://blog.marcgravell.com',
  connection_count: 980,
});

export const gregHewgill = imported({
  id: 893,
  handle: 'greg_hewgill_893',
  name: 'Greg Hewgill',
  category: 'Systems Programming',
  branch: 'c++',
  reputation: 970_924,
  influence: 99.98,
  created_at: '2008-08-10T08:27:00Z',
  location: 'Christchurch, New Zealand',
  website_url: 'https://hewgill.com',
  connection_count: 640,
});

export const quentin = imported({
  id: 19068,
  handle: 'quentin_19068',
  name: 'Quentin',
  category: 'Web Development',
  branch: 'javascript',
  reputation: 929_967,
  influence: 99.97,
  created_at: '2008-09-19T16:48:26Z',
  location: 'United Kingdom',
  connection_count: 312,
});

export const christian = imported({
  id: 5445,
  handle: 'christian_c_salvad_5445',
  name: 'Christian C. Salvadó',
  category: 'Web Development',
  branch: 'javascript',
  reputation: 817_238,
  influence: 99.95,
  created_at: '2008-09-09T16:04:18Z',
  location: 'Guatemala',
  website_url: 'http://codingspot.com',
  connection_count: 401,
});

export const bobince = imported({
  id: 18936,
  handle: 'bobince_18936',
  name: 'bobince',
  category: 'Web Development',
  branch: 'javascript',
  reputation: 532_163,
  influence: 99.88,
  created_at: '2008-09-17T20:27:26Z',
  location: 'Cambridge, UK',
  website_url: 'http://www.doxdesk.com/',
  connection_count: 288,
});

export const hansPassant = imported({
  id: 17034,
  handle: 'hans_passant_17034',
  name: 'Hans Passant',
  category: '.NET & Windows',
  branch: 'c#',
  reputation: 932_181,
  influence: 99.97,
  created_at: '2008-09-18T00:45:29Z',
  location: 'Madison, WI',
  connection_count: 150,
});

export const vonC = imported({
  id: 6309,
  handle: 'vonc_6309',
  name: 'VonC',
  category: 'Java & JVM',
  branch: 'java',
  reputation: 1_347_333,
  influence: 99.99,
  created_at: '2008-09-13T22:22:33Z',
  location: 'France',
  website_url: 'https://devstory.fyi/vonc',
  connection_count: 530,
});

export const people: UserProfileWithCount[] = [
  jonSkeet,
  vonC,
  marcGravell,
  gregHewgill,
  hansPassant,
  quentin,
  christian,
  bobince,
];

export function userRef(profile: UserProfileWithCount): UserRef {
  const { id, handle, name, category, branch, reputation } = profile;
  return { id, handle, name, category, branch, reputation };
}

export const meRef = userRef(me);

export const authResponse: AuthResponse = {
  token: 'test-token',
  expires_at: '2099-01-01T00:00:00Z',
  user: { id: me.id, handle: me.handle, name: me.name },
};

/** The member's own connections (member-created). */
export const myConnections: Connection[] = [
  {
    user: quentin,
    strength: 0.872,
    interactions: 0,
    origin: 'member',
    connected_at: '2026-10-19T09:05:00Z',
    first_interaction_at: null,
    last_interaction_at: null,
  },
  {
    user: bobince,
    strength: 0.7,
    interactions: 0,
    origin: 'member',
    connected_at: '2026-10-19T09:06:00Z',
    first_interaction_at: null,
    last_interaction_at: null,
  },
];

/** A real imported connection (Jon Skeet and Marc Gravell: 73 interactions in 2008). */
export const importedConnection: Connection = {
  user: marcGravell,
  strength: 1,
  interactions: 73,
  origin: 'imported',
  connected_at: '2008-09-29T13:59:45Z',
  first_interaction_at: '2008-09-29T13:59:45Z',
  last_interaction_at: '2008-12-31T15:33:11Z',
};

export const summary: AnalyticsSummary = {
  graph_version: 41,
  users: 18_649,
  connections: 204_670,
  density: 0.001177,
  average_degree: 21.95,
  components: 37,
  largest_component: 18_571,
  communities: 64,
  modularity: 0.4712,
  computed_at: '2026-10-19T09:58:00Z',
  compute_ms: 2_840,
  betweenness_approximate: true,
};

const INFLUENCER_SCORES: Record<InfluenceMetric, number[]> = {
  pagerank: [0.00382, 0.00197, 0.00174, 0.00121],
  betweenness: [0.0611, 0.0301, 0.0217, 0.0164],
  closeness: [0.412, 0.398, 0.391, 0.377],
  degree: [0.0647, 0.0525, 0.0343, 0.0284],
};

const INFLUENCER_ORDER: Record<InfluenceMetric, UserProfileWithCount[]> = {
  pagerank: [jonSkeet, marcGravell, vonC, gregHewgill],
  betweenness: [jonSkeet, vonC, gregHewgill, marcGravell],
  closeness: [jonSkeet, marcGravell, gregHewgill, vonC],
  degree: [jonSkeet, marcGravell, gregHewgill, vonC],
};

export function influencers(
  metric: InfluenceMetric,
  graphVersion = summary.graph_version,
): InfluencersResponse {
  return {
    graph_version: graphVersion,
    metric,
    items: INFLUENCER_ORDER[metric].map((profile, index) => ({
      rank: index + 1,
      user: userRef(profile),
      score: INFLUENCER_SCORES[metric][index] ?? 0,
    })),
  };
}

export const communities: CommunitiesResponse = {
  graph_version: summary.graph_version,
  modularity: summary.modularity,
  items: [
    {
      id: 3,
      size: 4_210,
      dominant_category: '.NET & Windows',
      dominant_branch: 'c#',
      members: [jonSkeet, marcGravell, hansPassant].map(userRef),
    },
    {
      id: 0,
      size: 3_115,
      dominant_category: 'Web Development',
      dominant_branch: 'javascript',
      members: [quentin, christian, bobince].map(userRef),
    },
    {
      id: 7,
      size: 1_904,
      dominant_category: 'Systems Programming',
      dominant_branch: 'c++',
      members: [gregHewgill].map(userRef),
    },
    {
      id: 12,
      size: 1_502,
      dominant_category: 'Java & JVM',
      dominant_branch: 'java',
      members: [vonC].map(userRef),
    },
  ],
};

const COMMUNITY_OF: Record<number, number> = {
  [jonSkeet.id]: 3,
  [marcGravell.id]: 3,
  [hansPassant.id]: 3,
  [quentin.id]: 0,
  [christian.id]: 0,
  [bobince.id]: 0,
  [gregHewgill.id]: 7,
  [vonC.id]: 12,
  [me.id]: 0,
};

const PAGERANK_OF: Record<number, number> = {
  [jonSkeet.id]: 0.00382,
  [marcGravell.id]: 0.00197,
  [vonC.id]: 0.00174,
  [gregHewgill.id]: 0.00121,
  [hansPassant.id]: 0.00098,
  [quentin.id]: 0.00087,
  [christian.id]: 0.00071,
  [bobince.id]: 0.00052,
  [me.id]: 0.00001,
};

/** Connections carry plain profiles; the fixture people also have their connection count. */
function graphNode(profile: UserProfile & Partial<Pick<UserProfileWithCount, 'connection_count'>>) {
  return {
    id: profile.id,
    handle: profile.handle,
    name: profile.name,
    category: profile.category,
    branch: profile.branch,
    reputation: profile.reputation,
    community: COMMUNITY_OF[profile.id] ?? 0,
    pagerank: PAGERANK_OF[profile.id] ?? 0,
    degree: profile.connection_count ?? 1,
  };
}

/** Real 2008 interactions between these people (strengths from connections.csv). */
const REAL_EDGES = [
  { source: gregHewgill.id, target: jonSkeet.id, strength: 0.292 },
  { source: vonC.id, target: jonSkeet.id, strength: 0.645 },
  { source: hansPassant.id, target: jonSkeet.id, strength: 0.642 },
  { source: hansPassant.id, target: marcGravell.id, strength: 0.743 },
  { source: jonSkeet.id, target: marcGravell.id, strength: 1 },
];

export function topGraph(limit = 1500): GraphResponse {
  return {
    graph_version: summary.graph_version,
    total_nodes: summary.users,
    total_edges: summary.connections,
    nodes: people.slice(0, limit).map(graphNode),
    edges: REAL_EDGES,
  };
}

export function egoGraph(focus: UserProfileWithCount, connections: Connection[]): GraphResponse {
  const nodes = [focus, ...connections.map((connection) => connection.user)].map(graphNode);
  return {
    graph_version: summary.graph_version,
    total_nodes: summary.users,
    total_edges: summary.connections,
    nodes,
    edges: connections.map((connection) => ({
      source: Math.min(focus.id, connection.user.id),
      target: Math.max(focus.id, connection.user.id),
      strength: connection.strength,
    })),
  };
}

export const recommendations: Recommendation[] = [
  {
    user: userRef(christian),
    score: 0.812,
    mutual_connections: 2,
    reasons: {
      adamic_adar: 1.44,
      affinity: 0.98,
      same_category: true,
      same_branch: true,
      same_community: true,
    },
  },
  {
    user: userRef(jonSkeet),
    score: 0.405,
    mutual_connections: 0,
    reasons: {
      adamic_adar: 0,
      affinity: 0.3,
      same_category: false,
      same_branch: false,
      same_community: false,
    },
  },
];

export function metricsFor(profile: UserProfileWithCount): UserMetrics {
  return {
    graph_version: summary.graph_version,
    user: userRef(profile),
    degree: profile.connection_count,
    pagerank: PAGERANK_OF[profile.id] ?? 0,
    betweenness: 0.0611,
    closeness: 0.412,
    community: COMMUNITY_OF[profile.id] ?? 0,
    ranks: { pagerank: 1, betweenness: 1, closeness: 1, degree: 1 },
  };
}

export function pathTo(target: UserProfileWithCount): PathResponse {
  return {
    graph_version: summary.graph_version,
    found: true,
    degrees: 3,
    path: [meRef, userRef(quentin), userRef(marcGravell), userRef(target)],
  };
}

export const jonSkeetActivity: UserActivity = {
  user_id: jonSkeet.id,
  totals: {
    questions: 3,
    answers: 2_918,
    accepted_answers: 1_340,
    comments: 4_120,
    question_score: 410,
    answer_score: 61_350,
  },
  top_tags: [
    { tag: 'c#', posts: 697, score: 37_394 },
    { tag: '.net', posts: 366, score: 24_009 },
    { tag: 'java', posts: 175, score: 12_792 },
    { tag: 'linq', posts: 83, score: 2_127 },
  ],
  badges: {
    gold: 24,
    silver: 482,
    bronze: 1_008,
    recent: [
      { name: 'Enlightened', class: 2, awarded_at: '2009-12-31T12:48:12Z' },
      { name: 'Nice Answer', class: 3, awarded_at: '2009-12-29T23:37:33Z' },
    ],
  },
  top_posts: [
    {
      id: 215422,
      type: 'answer',
      question_id: 7074,
      title: 'What is the difference between String and string in C#?',
      score: 3_809,
      view_count: null,
      created_at: '2008-10-18T18:52:13Z',
      tags: ['c#', '.net', 'string', 'types', 'alias'],
      url: 'https://stackoverflow.com/a/215422',
    },
    {
      id: 285184,
      type: 'answer',
      question_id: 285177,
      title: 'How do I call one constructor from another in Java?',
      score: 3_395,
      view_count: null,
      created_at: '2008-11-12T20:12:14Z',
      tags: 'java constructor',
      url: 'https://stackoverflow.com/a/285184',
    },
  ],
};

export function emptyActivity(userId: number): UserActivity {
  return {
    user_id: userId,
    totals: {
      questions: 0,
      answers: 0,
      accepted_answers: 0,
      comments: 0,
      question_score: 0,
      answer_score: 0,
    },
    top_tags: [],
    badges: { gold: 0, silver: 0, bronze: 0, recent: [] },
    top_posts: [],
  };
}

export const tags: Tag[] = [
  { name: 'c#', question_count: 7_456, category: '.NET & Windows' },
  { name: '.net', question_count: 5_873, category: '.NET & Windows' },
  { name: 'java', question_count: 4_278, category: 'Java & JVM' },
  { name: 'asp.net', question_count: 3_766, category: 'Web Development' },
  { name: 'c++', question_count: 3_090, category: 'Systems Programming' },
  { name: 'javascript', question_count: 2_724, category: 'Web Development' },
];

export const questions: QuestionSummary[] = [
  {
    id: 7074,
    title: 'What is the difference between String and string in C#?',
    tags: ['c#', '.net', 'string', 'types', 'alias'],
    score: 7_529,
    view_count: 1_308_690,
    answer_count: 68,
    created_at: '2008-08-10T07:18:02Z',
    owner: { id: 571, display_name: 'Lance Fisher' },
    url: 'https://stackoverflow.com/q/7074',
  },
  {
    id: 105372,
    title: 'How to enumerate an enum?',
    tags: ['c#', '.net', 'loops', 'enums', 'enumeration'],
    score: 4_378,
    view_count: 1_035_122,
    answer_count: 36,
    created_at: '2008-09-19T20:34:50Z',
    owner: { id: 12597, display_name: 'Ian Boyd' },
    url: 'https://stackoverflow.com/q/105372',
  },
  {
    id: 29482,
    title: 'How do I cast int to enum in C#?',
    tags: ['c#', 'enums', 'casting', 'integer'],
    score: 3_897,
    view_count: 2_044_744,
    answer_count: 32,
    created_at: '2008-08-27T03:58:21Z',
    owner: null,
    url: 'https://stackoverflow.com/q/29482',
  },
];

export const questionDetail: QuestionDetail = {
  ...questions[0]!,
  comment_count: 16,
  accepted_answer_id: 7077,
  closed_at: null,
  answers: [
    {
      id: 7077,
      owner: { id: 872, display_name: 'Derek Park' },
      score: 7_062,
      created_at: '2008-08-10T07:22:02Z',
      is_accepted: true,
      url: 'https://stackoverflow.com/a/7077',
    },
    {
      id: 215422,
      owner: { id: 22656, display_name: 'Jon Skeet' },
      score: 3_809,
      created_at: '2008-10-18T18:52:13Z',
      is_accepted: false,
      url: 'https://stackoverflow.com/a/215422',
    },
    {
      id: 215304,
      owner: { id: 13937, display_name: 'artur02' },
      score: 831,
      created_at: '2008-10-18T17:25:45Z',
      is_accepted: false,
    },
  ],
};
