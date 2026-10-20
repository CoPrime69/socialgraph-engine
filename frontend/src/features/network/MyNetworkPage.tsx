import { useSession } from '../../auth/authContext';
import { PageHeader } from '../../components/PageHeader';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { GraphCard } from '../graph/GraphCard';
import { ConnectionsCard } from './ConnectionsCard';
import { RecommendationsCard } from './RecommendationsCard';
import styles from './network.module.css';

/** Ego-network size for "my network" (most influential first). */
const MY_GRAPH_LIMIT = 300;

export function MyNetworkPage() {
  useDocumentTitle('My network');
  const session = useSession();
  return (
    <div className={styles.page}>
      <PageHeader
        title="My network"
        description="The people you are connected to, and suggestions for who to connect with next."
      />
      <div className={styles.columns}>
        <ConnectionsCard />
        <RecommendationsCard />
      </div>
      <GraphCard
        title="Your network graph"
        description="You, your connections and how they are connected to each other."
        scope={{ kind: 'ego', focusId: session.user.id, limit: MY_GRAPH_LIMIT }}
        youId={session.user.id}
        size="md"
        emptyMessage="Connect with people from your suggestions to see your network here."
      />
    </div>
  );
}
