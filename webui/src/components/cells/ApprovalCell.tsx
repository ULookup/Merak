import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import styles from './Cells.module.css';

interface Props {
  approvalId: string;
  approvalName: string;
  approvalArgs?: string;
}

export default function ApprovalCell({ approvalId, approvalName, approvalArgs }: Props) {
  const { dispatch } = useAppState();

  async function handle(allow: boolean) {
    try {
      await api.resolveApproval(approvalId, allow);
      dispatch({ type: 'CLEAR_APPROVAL' });
    } catch (e) {
      dispatch({
        type: 'APPEND_MESSAGE',
        message: {
          id: 'err_' + Date.now(),
          kind: 'system',
          text: `Approval error: ${e}`,
          error: true,
        },
      });
    }
  }

  return (
    <div className={styles.approval}>
      <span className={styles.approvalLabel}>Approval needed</span>
      <span>
        {approvalName}
        {approvalArgs ? ` ${approvalArgs}` : ''}
      </span>
      <button className={styles.btnAllow} onClick={() => handle(true)} data-testid="approval-allow">
        Allow
      </button>
      <button className={styles.btnDeny} onClick={() => handle(false)} data-testid="approval-deny">
        Deny
      </button>
    </div>
  );
}
