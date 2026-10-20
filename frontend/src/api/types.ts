/**
 * TypeScript mirror of the HTTP contract in docs/contracts.md (§3-§6, superseded where
 * applicable by §10 "contract v2", the Stack Overflow 2008 dataset).
 * Field names are kept in snake_case exactly as they travel on the wire.
 */

/** RFC 3339 UTC timestamp, e.g. "2026-10-19T20:00:00Z". */
export type Timestamp = string;

/** Values come from GET /api/v1/meta/taxonomy (§10.3), so they are not literal unions. */
export type Category = string;
export type Branch = string;

// ---------- Common ----------

export interface Paginated<T> {
  items: T[];
  total: number;
  limit: number;
  offset: number;
}

export interface FieldErrorDetail {
  field: string;
  message: string;
}

export interface ErrorEnvelope {
  error: {
    code: string;
    message: string;
    details?: FieldErrorDetail[];
  };
}

// ---------- identity-service (§4, §10.4) ----------

export interface AuthUser {
  id: number;
  handle: string;
  name: string;
}

export interface AuthResponse {
  token: string;
  expires_at: Timestamp;
  user: AuthUser;
}

export interface LoginRequest {
  handle: string;
  password: string;
}

export interface RegisterRequest {
  handle: string;
  name: string;
  password: string;
  category: Category;
  branch: Branch;
  location?: string;
  /** At most 500 characters. */
  about?: string;
}

// ---------- graph-service (§5, §10.5) ----------

export interface Taxonomy {
  categories: Category[];
  branches: Branch[];
  /** Maps every branch to the name of the category it belongs to. */
  branch_categories: Record<Branch, Category>;
}

export type ProfileSource = 'stackoverflow' | 'member';

export interface UserProfile {
  id: number;
  handle: string;
  name: string;
  category: Category;
  branch: Branch;
  /** Reputation percentile, 0-100 (2 dp). */
  influence: number;
  reputation: number;
  location: string | null;
  website_url: string | null;
  /** Free text; imported Stack Overflow profiles may contain HTML markup. */
  about: string | null;
  views: number;
  up_votes: number;
  down_votes: number;
  source: ProfileSource;
  /** `https://stackoverflow.com/users/<id>` for imported users, otherwise null. */
  external_url: string | null;
  created_at: Timestamp;
  last_access_at: Timestamp | null;
}

export interface UserProfileWithCount extends UserProfile {
  connection_count: number;
}

export type UserSort = 'reputation' | 'name' | 'id';

export interface UserListParams {
  q?: string | undefined;
  category?: Category | undefined;
  branch?: Branch | undefined;
  sort?: UserSort | undefined;
  min_reputation?: number | undefined;
  limit?: number | undefined;
  offset?: number | undefined;
}

export type ConnectionOrigin = 'imported' | 'member';

export interface Connection {
  user: UserProfile;
  /** Connection strength in [0, 1]. */
  strength: number;
  /** Number of real Stack Overflow interactions (answers/comments) for imported connections. */
  interactions: number;
  origin: ConnectionOrigin;
  connected_at: Timestamp;
  first_interaction_at: Timestamp | null;
  last_interaction_at: Timestamp | null;
}

export interface ConnectionsResponse {
  items: Connection[];
  graph_version: number;
}

export interface CreateConnectionResponse extends Connection {
  graph_version: number;
}

export interface RemoveConnectionResponse {
  graph_version: number;
}

// ---------- analytics-service (§6) ----------

export interface UserRef {
  id: number;
  handle: string;
  name: string;
  category: Category;
  branch: Branch;
  reputation: number;
}

export interface Versioned {
  graph_version: number;
}

export interface AnalyticsSummary extends Versioned {
  users: number;
  connections: number;
  density: number;
  average_degree: number;
  components: number;
  largest_component: number;
  communities: number;
  modularity: number;
  computed_at: Timestamp;
  compute_ms: number;
  /** Not in contracts.md; sent by analytics-service when betweenness uses pivot sampling. */
  betweenness_approximate?: boolean;
}

export type InfluenceMetric = 'pagerank' | 'betweenness' | 'closeness' | 'degree';

export interface InfluencerItem {
  rank: number;
  user: UserRef;
  score: number;
}

export interface InfluencersResponse extends Versioned {
  metric: InfluenceMetric;
  items: InfluencerItem[];
}

export interface Community {
  id: number;
  size: number;
  dominant_category: Category;
  dominant_branch: Branch;
  members: UserRef[];
}

export interface CommunitiesResponse extends Versioned {
  modularity: number;
  items: Community[];
}

export interface GraphNode {
  id: number;
  handle: string;
  name: string;
  category: Category;
  branch: Branch;
  reputation: number;
  community: number;
  pagerank: number;
  degree: number;
}

export interface GraphEdge {
  source: number;
  target: number;
  strength: number;
}

export interface GraphResponse extends Versioned {
  /** Size of the whole network, of which `nodes`/`edges` are a subset. */
  total_nodes: number;
  total_edges: number;
  nodes: GraphNode[];
  /** Edges induced by the returned nodes. */
  edges: GraphEdge[];
}

/**
 * `GET /api/v1/analytics/graph` scope: the top `limit` users by PageRank, or the ego network
 * of `focus` up to `depth` hops (most influential first, capped by `limit`).
 */
export interface GraphQuery {
  limit: number;
  focus?: number | undefined;
  depth?: 1 | 2 | undefined;
}

export interface RecommendationReasons {
  adamic_adar: number;
  affinity: number;
  same_category: boolean;
  same_branch: boolean;
  same_community: boolean;
}

export interface Recommendation {
  user: UserRef;
  score: number;
  mutual_connections: number;
  reasons: RecommendationReasons;
}

export interface RecommendationsResponse extends Versioned {
  items: Recommendation[];
}

export interface PathResponse extends Versioned {
  found: boolean;
  /** Number of edges on the path; null when no path exists. */
  degrees: number | null;
  path: UserRef[];
}

export interface UserMetrics extends Versioned {
  user: UserRef;
  degree: number;
  pagerank: number;
  betweenness: number;
  closeness: number;
  community: number;
  ranks: Record<InfluenceMetric, number>;
}

/**
 * Every analytics response is decorated with `stale`, taken from the `X-Graph-Stale`
 * response header (§6): true when the read model had not reached the requested
 * `min_version` within the server's 2 s wait.
 */
export type WithFreshness<T> = T & { stale: boolean };

// ---------- content-service (§10.6) ----------

export interface ActivityTotals {
  questions: number;
  answers: number;
  accepted_answers: number;
  comments: number;
  question_score: number;
  answer_score: number;
}

export interface TagExpertise {
  tag: string;
  posts: number;
  score: number;
}

/** 1 = gold, 2 = silver, 3 = bronze. */
export type BadgeClass = 1 | 2 | 3;

export interface BadgeAward {
  name: string;
  class: BadgeClass;
  awarded_at: Timestamp;
}

export interface BadgeSummary {
  gold: number;
  silver: number;
  bronze: number;
  recent: BadgeAward[];
}

/** Tags may arrive as an array or as the dataset's space-separated string; see `normalizeTags`. */
export type TagList = string[] | string;

export interface TopPost {
  id: number;
  type: 'question' | 'answer';
  /** Not in contracts.md; content-service adds the parent question of an answer. */
  question_id?: number;
  /** For answers this is the question's title. */
  title: string;
  score: number;
  /** content-service sends null for answers, which have no view count of their own. */
  view_count: number | null;
  created_at: Timestamp;
  tags: TagList;
  url: string;
}

export interface UserActivity {
  user_id: number;
  totals: ActivityTotals;
  top_tags: TagExpertise[];
  badges: BadgeSummary;
  top_posts: TopPost[];
}

export interface Tag {
  name: string;
  question_count: number;
  category: Category | null;
}

export interface TagListParams {
  q?: string | undefined;
  limit?: number | undefined;
  offset?: number | undefined;
}

export interface PostOwner {
  id: number;
  display_name: string;
}

export type QuestionSort = 'score' | 'recent';

export interface QuestionListParams {
  tag?: string | undefined;
  q?: string | undefined;
  sort?: QuestionSort | undefined;
  limit?: number | undefined;
  offset?: number | undefined;
}

export interface QuestionSummary {
  id: number;
  title: string;
  tags: TagList;
  score: number;
  view_count: number;
  answer_count: number;
  created_at: Timestamp;
  owner: PostOwner | null;
  url: string;
}

export interface Answer {
  id: number;
  owner: PostOwner | null;
  score: number;
  created_at: Timestamp;
  is_accepted: boolean;
  /** Not in contracts.md; sent by content-service (`https://stackoverflow.com/a/<id>`). */
  url?: string;
}

export interface QuestionDetail extends QuestionSummary {
  answers: Answer[];
  comment_count: number;
  /** Not in contracts.md; sent by content-service. */
  accepted_answer_id?: number | null;
  /** Not in contracts.md; sent by content-service when the question was closed. */
  closed_at?: Timestamp | null;
}
