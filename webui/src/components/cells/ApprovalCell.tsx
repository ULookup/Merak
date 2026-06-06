import { useState } from 'react';
import { ChevronDown, ChevronUp } from 'lucide-react';
import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import { useToast } from '../Toast';
import styles from './Cells.module.css';

const ARGS_PREVIEW_LEN = 120;

interface Props {
  approvalId: string;
  approvalName: string;
  approvalArgs?: string;
  approvalResolved?: boolean;
  approvalDecision?: string;
}

export default function ApprovalCell({
  approvalId,
  approvalName,
  approvalArgs,
  approvalResolved,
  approvalDecision,
}: Props) {
  const { dispatch } = useAppState();
  const { showToast } = useToast();
  const [pending, setPending] = useState(false);
  const [expanded, setExpanded] = useState(false);

  async function handle(allow: boolean) {
    setPending(true);
    try {
      await api.resolveApproval(approvalId, allow);
      dispatch({
        type: 'CLEAR_APPROVAL',
        decision: allow ? 'allowed' : 'denied',
      });
    } catch (e) {
      showToast(`Approval error: ${e}`, 'error');
      setPending(false);
    }
  }

  const decisionLabel =
    approvalDecision === 'allowed' ? 'Allowed' : approvalDecision === 'denied' ? 'Denied' : 'Resolved';

  const hasArgs = typeof approvalArgs === 'string' && approvalArgs.length > 0;
  const argsLong = hasArgs && approvalArgs!.length > ARGS_PREVIEW_LEN;
  const argsPreview =
    hasArgs && argsLong ? approvalArgs!.slice(0, ARGS_PREVIEW_LEN) + '…' : (approvalArgs ?? '');

  const argsBlock = hasArgs ? (
    <div className={styles.argsArea}>
      {argsLong ? (
        <pre className={styles.argsPre}>{expanded ? approvalArgs : argsPreview}</pre>
      ) : (
        <pre className={styles.argsPre}>{approvalArgs}</pre>
      )}
      {argsLong && (
        <button
          className={styles.argsToggle}
          onClick={() => setExpanded((v) => !v)}
          aria-label={expanded ? 'Collapse arguments' : 'Expand arguments'}
        >
          {expanded ? <ChevronUp size={12} /> : <ChevronDown size={12} />}
          {expanded ? 'Collapse' : 'Show all'}
        </button>
      )}
    </div>
  ) : null;

  if (approvalResolved) {
    return (
      <div className={`${styles.approval} ${styles.approvalResolved}`}>
        <div className={styles.approvalRow}>
          <span className={styles.approvalLabel}>{decisionLabel}</span>
          <span className={styles.approvalName}>{approvalName}</span>
        </div>
        {argsBlock}
      </div>
    );
  }

  return (
    <div className={styles.approval}>
      <div className={styles.approvalRow}>
        <span className={styles.approvalLabel}>Approval needed</span>
        <span className={styles.approvalName}>{approvalName}</span>
        <button
          className={styles.btnAllow}
          onClick={() => handle(true)}
          disabled={pending}
          data-testid="approval-allow"
          aria-label="Allow tool"
        >
          Allow
        </button>
        <button
          className={styles.btnDeny}
          onClick={() => handle(false)}
          disabled={pending}
          data-testid="approval-deny"
          aria-label="Deny tool"
        >
          Deny
        </button>
      </div>
      {argsBlock}
    </div>
  );
}
