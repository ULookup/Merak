import { useAppState } from '../../AppState';
import { api } from '../../api/client';

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
        message: { id: 'err_' + Date.now(), kind: 'system', text: `Approval error: ${e}`, error: true },
      });
    }
  }

  return (
    <div className="msg-approval">
      <span className="approval-label">Approval needed</span>
      <span>{approvalName}{approvalArgs ? ` ${approvalArgs}` : ''}</span>
      <button className="btn-allow" onClick={() => handle(true)}>Allow</button>
      <button className="btn-deny" onClick={() => handle(false)}>Deny</button>
    </div>
  );
}
