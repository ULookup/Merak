import { api } from '../../api/client';
import { useAppState } from '../../AppState';
import { useToast } from '../Toast';
import styles from './Cells.module.css';

interface Props {
  approvalId: string;
  approvalName: string;
  approvalArgs?: string;
}

export default function ApprovalCell({ approvalId, approvalName, approvalArgs }: Props) {
  const { dispatch } = useAppState();
  const { showToast } = useToast();

  async function handle(allow: boolean) {
    try {
      await api.resolveApproval(approvalId, allow);
      dispatch({ type: 'CLEAR_APPROVAL' });
    } catch (e) {
      showToast(`Approval error: ${e}`, 'error');
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
