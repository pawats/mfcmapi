// Editor.cpp : implementation file
//

#include "stdafx.h"
#include "Editor.h"
#include "MFCUtilityFunctions.h"
#include "MAPIFunctions.h"
#include "InterpretProp.h"
#include "ImportProcs.h"

// tmschema.h has been deprecated, but older compilers do not ship vssym32.h
// Use the replacement when we're on VS 2008 or higher.
#if defined(_MSC_VER) && (_MSC_VER >= 1500)
#include <vssym32.h>
#else
#include <tmschema.h>
#endif

#include "AboutDlg.h"

static TCHAR* CLASS = _T("CEditor");

__ListButtons ListButtons[NUMLISTBUTTONS] = {
	{IDD_LISTMOVEDOWN},
	{IDD_LISTMOVETOBOTTOM},
	{IDD_LISTADD},
	{IDD_LISTEDIT},
	{IDD_LISTDELETE},
	{IDD_LISTMOVETOTOP},
	{IDD_LISTMOVEUP},
};

#define NOLIST 0XFFFFFFFF

#define MAX_WIDTH 1000

#define INVALIDRANGE(iVal) ((iVal) >= m_cControls)

static DWORD CALLBACK EditStreamReadCallBack(
											  DWORD_PTR dwCookie,
											  LPBYTE pbBuff,
											  LONG cb,
											  LONG *pcb)
{
	HRESULT hRes = S_OK;
	if (!pbBuff || !pcb || !dwCookie) return 0;

	LPSTREAM stmData = (LPSTREAM) dwCookie;
	ULONG cbRead = 0;

	*pcb = 0;

	DebugPrint(DBGGeneric,_T("EditStreamWriteCallBack:cb = %d\n"),cb);

	EC_H(stmData->Read(pbBuff,cb,&cbRead));

	DebugPrint(DBGGeneric,_T("EditStreamReadCallBack: read %d bytes\n"),cbRead);

	*pcb = cbRead;

	return 0;
} // EditStreamReadCallBack

static DWORD CALLBACK EditStreamWriteCallBack(
											  DWORD_PTR dwCookie,
											  LPBYTE pbBuff,
											  LONG cb,
											  LONG *pcb)
{
	HRESULT hRes = S_OK;
	if (!pbBuff || !pcb || !dwCookie) return 0;

	LPSTREAM stmData = (LPSTREAM) dwCookie;
	ULONG cbWritten = 0;

	*pcb = 0;

	DebugPrint(DBGGeneric,_T("EditStreamWriteCallBack:cb = %d\n"),cb);

	EC_H(stmData->Write(pbBuff,cb,&cbWritten));

	DebugPrint(DBGGeneric,_T("EditStreamWriteCallBack: wrote %d bytes\n"),cbWritten);

	*pcb = cbWritten;

	return 0;
} // EditStreamWriteCallBack

// Use this constuctor for generic data editing
CEditor::CEditor(
				 CWnd* pParentWnd,
				 UINT uidTitle,
				 UINT uidPrompt,
				 ULONG ulNumFields,
				 ULONG ulButtonFlags):CDialog(IDD_BLANK_DIALOG,pParentWnd)
{
	Constructor(pParentWnd,
		uidTitle,
		uidPrompt,
		ulNumFields,
		ulButtonFlags,
		IDS_ACTION1,
		IDS_ACTION2);
} // CEditor::CEditor

CEditor::CEditor(
				 CWnd* pParentWnd,
				 UINT uidTitle,
				 UINT uidPrompt,
				 ULONG ulNumFields,
				 ULONG ulButtonFlags,
				 UINT uidActionButtonText1,
				 UINT uidActionButtonText2):CDialog(IDD_BLANK_DIALOG,pParentWnd)
{
	Constructor(pParentWnd,
		uidTitle,
		uidPrompt,
		ulNumFields,
		ulButtonFlags,
		uidActionButtonText1,
		uidActionButtonText2);
} // CEditor::CEditor

void CEditor::Constructor(
				 CWnd* pParentWnd,
				 UINT uidTitle,
				 UINT uidPrompt,
				 ULONG ulNumFields,
				 ULONG ulButtonFlags,
				 UINT uidActionButtonText1,
				 UINT uidActionButtonText2)
{
	TRACE_CONSTRUCTOR(CLASS);

	m_ulListNum = NOLIST;

	m_bButtonFlags = ulButtonFlags;

	m_iMargin = GetSystemMetrics(SM_CXHSCROLL)/2+1;

	m_lpControls = 0;
	m_cControls = 0;

	m_uidTitle = uidTitle;
	m_uidPrompt = uidPrompt;

	m_uidActionButtonText1 = uidActionButtonText1;
	m_uidActionButtonText2 = uidActionButtonText2;

	m_cButtons = 0;
	if (m_bButtonFlags & CEDITOR_BUTTON_OK)			m_cButtons++;
	if (m_bButtonFlags & CEDITOR_BUTTON_ACTION1)	m_cButtons++;
	if (m_bButtonFlags & CEDITOR_BUTTON_ACTION2)	m_cButtons++;
	if (m_bButtonFlags & CEDITOR_BUTTON_CANCEL)		m_cButtons++;

	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	HRESULT hRes = S_OK;
	WC_D(m_hIcon,AfxGetApp()->LoadIcon(IDR_MAINFRAME));

	m_pParentWnd = pParentWnd;
	if (!m_pParentWnd)
	{
		m_pParentWnd = GetForegroundWindow();
	}
	if (!m_pParentWnd)
	{
		m_pParentWnd = GetDesktopWindow();
	}
	if (!m_pParentWnd)
	{
		DebugPrint(DBGGeneric,_T("Editor created with a NULL parent!\n"));
	}
	if (ulNumFields) CreateControls(ulNumFields);
} // CEditor::Constructor

CEditor::~CEditor()
{
	TRACE_DESTRUCTOR(CLASS);
	DeleteControls();
} // CEditor::~CEditor

BEGIN_MESSAGE_MAP(CEditor, CDialog)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_PAINT()
	ON_WM_NCHITTEST()
END_MESSAGE_MAP()

LRESULT CEditor::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_HELP:
		DisplayAboutDlg(this);
		return TRUE;
		break;

		// I can handle notify messages for my child list control since I am the parent window
		// This makes it easy for me to customize the child control to do what I want
	case WM_NOTIFY:
		{
			LPNMHDR pHdr = (LPNMHDR) lParam;

			switch(pHdr->code)
			{
			case NM_DBLCLK:
			case NM_RETURN:
				OnEditListEntry(m_ulListNum);
				return NULL;
				break;
			}
			break;
		}
	case WM_COMMAND:
		{
			WORD nCode = HIWORD(wParam);
			WORD idFrom = LOWORD(wParam);
			if (EN_CHANGE == nCode)
			{
				HandleChange(idFrom);
			}
			else if (CBN_SELCHANGE == nCode)
			{
				HandleChange(idFrom);
			}
			else if (CBN_EDITCHANGE == nCode)
			{
				HandleChange(idFrom);
			}
			else if (BN_CLICKED == nCode)
			{
				switch(idFrom)
				{
				case IDD_LISTMOVEDOWN:		OnMoveListEntryDown(m_ulListNum); return NULL;
				case IDD_LISTADD:			OnAddListEntry(m_ulListNum); return NULL;
				case IDD_LISTEDIT:			OnEditListEntry(m_ulListNum); return NULL;
				case IDD_LISTDELETE:		OnDeleteListEntry(m_ulListNum,true); return NULL;
				case IDD_LISTMOVEUP:		OnMoveListEntryUp(m_ulListNum); return NULL;
				case IDD_LISTMOVETOBOTTOM:	OnMoveListEntryToBottom(m_ulListNum); return NULL;
				case IDD_LISTMOVETOTOP:		OnMoveListEntryToTop(m_ulListNum); return NULL;
				case IDD_EDITACTION1:		OnEditAction1(); return NULL;
				case IDD_EDITACTION2:		OnEditAction2(); return NULL;
				default:					HandleChange(idFrom); break;
				}
			}
			break;
		}
	} // end switch
	return CDialog::WindowProc(message,wParam,lParam);
} // CEditor::WindowProc

// AddIn functions
void CEditor::SetAddInTitle(LPWSTR szTitle)
{
#ifdef UNICODE
	m_szAddInTitle = szTitle;
#else
	LPSTR szTitleA = NULL;
	UnicodeToAnsi(szTitle,&szTitleA);
	m_szAddInTitle = szTitleA;
	delete[] szTitleA;
#endif
} // CEditor::SetAddInTitle

void CEditor::SetAddInLabel(ULONG i,LPWSTR szLabel)
{
	if (INVALIDRANGE(i)) return;
#ifdef UNICODE
	m_lpControls[i].szLabel = szLabel;
#else
	LPSTR szLabelA = NULL;
	UnicodeToAnsi(szLabel,&szLabelA);
	m_lpControls[i].szLabel = szLabelA;
	delete[] szLabelA;
#endif
} // CEditor::SetAddInLabel

// The order these controls are created dictates our tab order - be careful moving things around!
BOOL CEditor::OnInitDialog()
{
	HRESULT hRes = S_OK;
	CString szPrefix;
	CString szPostfix;
	CString szFullString;

	BOOL bRet = CDialog::OnInitDialog();

	szPostfix.LoadString(m_uidTitle);
	m_szTitle = szPostfix+m_szAddInTitle;
	SetWindowText(m_szTitle);

	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	if (m_uidPrompt)
	{
		szPrefix.LoadString(m_uidPrompt);
	}
	else
	{
		// Make sure we clear the prefix out or it might show up in the prompt
		szPrefix = _T(""); // STRING_OK
	}
	szFullString = szPrefix+m_szPromptPostFix;

	EC_B(m_Prompt.Create(
		WS_CHILD
		| WS_CLIPSIBLINGS
		| ES_MULTILINE
		| ES_READONLY
		| WS_VISIBLE,
		CRect(0,0,0,0),
		this,
		IDC_PROMPT));
	m_Prompt.SetWindowText(szFullString);
	m_Prompt.SetFont(GetFont());

	// we'll update this along the way
	m_iButtonWidth = 50;

	// setup to get button widths
	CDC* dcSB = GetDC();
	if (!dcSB) return false; // fatal error
	CFont* pFont = NULL; // will get this as soon as we've got a button to get it from
	SIZE sizeText = {0};

	for (ULONG i = 0 ; i < m_cControls ; i++)
	{
		UINT iCurIDLabel	= IDC_PROP_CONTROL_ID_BASE+2*i;
		UINT iCurIDControl	= IDC_PROP_CONTROL_ID_BASE+2*i+1;

		// Load up our strings
		// If uidLabel is NULL, then szLabel might already be set as an Add-In Label
		if (m_lpControls[i].uidLabel)
		{
			m_lpControls[i].szLabel.LoadString(m_lpControls[i].uidLabel);
		}

		if (m_lpControls[i].bUseLabelControl)
		{
			EC_B(m_lpControls[i].Label.Create(
				WS_CHILD
				| WS_CLIPSIBLINGS
				| ES_AUTOVSCROLL
				| ES_READONLY
				| WS_VISIBLE,
				CRect(0,0,0,0),
				this,
				iCurIDLabel));
			m_lpControls[i].Label.SetWindowText(m_lpControls[i].szLabel);
			m_lpControls[i].Label.SetFont(GetFont());
		}

		m_lpControls[i].nID = iCurIDControl;

		switch (m_lpControls[i].ulCtrlType)
		{
		case CTRL_EDIT:
			if (m_lpControls[i].UI.lpEdit)
			{
				EC_B(m_lpControls[i].UI.lpEdit->EditBox.Create(
					WS_TABSTOP
					| WS_CHILD
					| WS_CLIPSIBLINGS
					| WS_BORDER
					| WS_VISIBLE
					| WS_VSCROLL
					| ES_AUTOVSCROLL
					| (m_lpControls[i].bReadOnly?ES_READONLY:0)
					| (m_lpControls[i].UI.lpEdit->bMultiline?(ES_MULTILINE| ES_WANTRETURN):(ES_AUTOHSCROLL)),
					CRect(0,0,0,0),
					this,
					iCurIDControl));
				if (m_lpControls[i].bReadOnly) SetEditReadOnly(i);

				// Set maximum text size
				(void)::SendMessage(
					m_lpControls[i].UI.lpEdit->EditBox.m_hWnd,
					EM_EXLIMITTEXT,
					(WPARAM) 0,
					(LPARAM) 0);

				SetEditBoxText(i);

				m_lpControls[i].UI.lpEdit->EditBox.SetFont(GetFont());

				m_lpControls[i].UI.lpEdit->EditBox.SetEventMask(ENM_CHANGE);

				// Clear the modify bits so we can detect changes later
				m_lpControls[i].UI.lpEdit->EditBox.SetModify(false);
			}
			break;
		case CTRL_CHECK:
			if (m_lpControls[i].UI.lpCheck)
			{
				EC_B(m_lpControls[i].UI.lpCheck->Check.Create(
					NULL,
					WS_TABSTOP
					| WS_CHILD
					| WS_CLIPSIBLINGS
					| WS_VISIBLE
					| BS_AUTOCHECKBOX
					| (m_lpControls[i].bReadOnly?WS_DISABLED :0),
					CRect(0,0,0,0),
					this,
					iCurIDControl));
				m_lpControls[i].UI.lpCheck->Check.SetCheck(
					m_lpControls[i].UI.lpCheck->bCheckValue);
				m_lpControls[i].UI.lpCheck->Check.SetFont(GetFont());
				m_lpControls[i].UI.lpCheck->Check.SetWindowText(
					m_lpControls[i].szLabel);
			}
			break;
		case CTRL_LIST:
			if (m_lpControls[i].UI.lpList)
			{
				DWORD dwListStyle = LVS_SINGLESEL;
				if (!m_lpControls[i].UI.lpList->bAllowSort)
					dwListStyle |= LVS_NOSORTHEADER;
				EC_H(m_lpControls[i].UI.lpList->List.Create(this,dwListStyle,iCurIDControl,false));

				m_lpControls[i].UI.lpList->List.SetFont(GetFont());

				// read only lists don't need buttons
				if (!m_lpControls[i].bReadOnly)
				{
					int iButton = 0;

					for (iButton = 0; iButton <NUMLISTBUTTONS; iButton++)
					{
						CString szButtonText;
						szButtonText.LoadString(ListButtons[iButton].uiButtonID);

						EC_B(m_lpControls[i].UI.lpList->ButtonArray[iButton].Create(
							szButtonText,
							WS_TABSTOP
							| WS_CHILD
							| WS_CLIPSIBLINGS
							| WS_VISIBLE,
							CRect(0,0,0,0),
							this,
							ListButtons[iButton].uiButtonID));
						if (!pFont)
						{
							pFont = dcSB->SelectObject(m_lpControls[i].UI.lpList->ButtonArray[iButton].GetFont());
						}

						sizeText = dcSB->GetTabbedTextExtent(szButtonText,0,0);
						m_iButtonWidth = max(m_iButtonWidth,sizeText.cx);
					}
				}
			}
			break;
		case CTRL_DROPDOWN:
			if (m_lpControls[i].UI.lpDropDown)
			{
				// bReadOnly means you can't type...
				DWORD dwDropStyle;
				if (m_lpControls[i].bReadOnly)
				{
					dwDropStyle = CBS_DROPDOWNLIST; // does not allow typing
				}
				else
				{
					dwDropStyle = CBS_DROPDOWN; // allows typing
				}
				EC_B(m_lpControls[i].UI.lpDropDown->DropDown.Create(
					WS_TABSTOP
					| WS_CHILD
					| WS_CLIPSIBLINGS
					| WS_BORDER
					| WS_VISIBLE
					| WS_VSCROLL
					| CBS_AUTOHSCROLL
					| CBS_DISABLENOSCROLL
					| dwDropStyle,
					CRect(0,0,0,0),
					this,
					iCurIDControl));
				m_lpControls[i].UI.lpDropDown->DropDown.SetFont(GetFont());
				if (m_lpControls[i].UI.lpDropDown->lpuidDropList)
				{
					for (ULONG iDropNum=0 ; iDropNum < m_lpControls[i].UI.lpDropDown->ulDropList ; iDropNum++)
					{
						CString szDropString;
						szDropString.LoadString(m_lpControls[i].UI.lpDropDown->lpuidDropList[iDropNum]);
						m_lpControls[i].UI.lpDropDown->DropDown.InsertString(
							iDropNum,
							szDropString);
					}
				}
				m_lpControls[i].UI.lpDropDown->DropDown.SetCurSel(0);
			}
			break;
		}
	}

	if (m_bButtonFlags & CEDITOR_BUTTON_OK)
	{
		CString szOk;
		szOk.LoadString(IDS_OK);
		EC_B(m_OkButton.Create(
			szOk,
			WS_TABSTOP
			| WS_CHILD
			| WS_CLIPSIBLINGS
			| WS_VISIBLE,
			CRect(0,0,0,0),
			this,
			IDOK));
	}

	if (m_bButtonFlags & CEDITOR_BUTTON_ACTION1)
	{
		CString szActionButtonText1;
		szActionButtonText1.LoadString(m_uidActionButtonText1);
		EC_B(m_ActionButton1.Create(
			szActionButtonText1,
			WS_TABSTOP
			| WS_CHILD
			| WS_CLIPSIBLINGS
			| WS_VISIBLE,
			CRect(0,0,0,0),
			this,
			IDD_EDITACTION1));
		if (!pFont)
		{
			pFont = dcSB->SelectObject(m_ActionButton1.GetFont());
		}
		sizeText = dcSB->GetTabbedTextExtent(szActionButtonText1,0,0);
		m_iButtonWidth = max(m_iButtonWidth,sizeText.cx);
	}

	if (m_bButtonFlags & CEDITOR_BUTTON_ACTION2)
	{
		CString szActionButtonText2;
		szActionButtonText2.LoadString(m_uidActionButtonText2);
		EC_B(m_ActionButton2.Create(
			szActionButtonText2,
			WS_TABSTOP
			| WS_CHILD
			| WS_CLIPSIBLINGS
			| WS_VISIBLE,
			CRect(0,0,0,0),
			this,
			IDD_EDITACTION2));
		if (!pFont)
		{
			pFont = dcSB->SelectObject(m_ActionButton2.GetFont());
		}
		sizeText = dcSB->GetTabbedTextExtent(szActionButtonText2,0,0);
		m_iButtonWidth = max(m_iButtonWidth,sizeText.cx);
	}

	if (m_bButtonFlags & CEDITOR_BUTTON_CANCEL)
	{
		CString szCancel;
		szCancel.LoadString(IDS_CANCEL);
		EC_B(m_CancelButton.Create(
			szCancel,
			WS_TABSTOP
			| WS_CHILD
			| WS_CLIPSIBLINGS
			| WS_VISIBLE,
			CRect(0,0,0,0),
			this,
			IDCANCEL));
	}

	// tear down from our width computations
	if (pFont)
	{
		dcSB->SelectObject(pFont);
	}
	ReleaseDC(dcSB);
	m_iButtonWidth += m_iMargin;

	// Compute some constants
	m_iEditHeight = GetEditHeight(m_Prompt);
	m_iTextHeight = GetTextHeight(m_Prompt);
	m_iButtonHeight = m_iTextHeight + GetSystemMetrics(SM_CYEDGE)*2;

	OnSetDefaultSize();

	// Remove the awful autoselect of the first edit control that scrolls to the end of multiline text
	if (m_cControls &&
		m_lpControls &&
		CTRL_EDIT == m_lpControls[0].ulCtrlType &&
		m_lpControls[0].UI.lpEdit->bMultiline)
	{
		::PostMessage(
			m_lpControls[0].UI.lpEdit->EditBox.m_hWnd,
			EM_SETSEL,
			(WPARAM) 0,
			(LPARAM) 0);
	}

	return bRet;
} // CEditor::OnInitDialog

void CEditor::OnOK()
{
	// save data from the UI back into variables that we can query later
	for (ULONG j = 0 ; j < m_cControls ; j++)
	{
		switch (m_lpControls[j].ulCtrlType)
		{
		case CTRL_EDIT:
			if (m_lpControls[j].UI.lpEdit)
			{
				GetEditBoxText(j);
			}
			break;
		case CTRL_CHECK:
			if (m_lpControls[j].UI.lpCheck)
				m_lpControls[j].UI.lpCheck->bCheckValue = m_lpControls[j].UI.lpCheck->Check.GetCheck();
			break;
		case CTRL_LIST:
			break;
		case CTRL_DROPDOWN:
			if (m_lpControls[j].UI.lpDropDown)
				m_lpControls[j].UI.lpDropDown->iDropValue = GetDropDownSelection(j);
			break;
		}
	}
	CDialog::OnOK();
} // CEditor::OnOK

HRESULT CEditor::DisplayDialog()
{
	HRESULT hRes = S_OK;
	INT_PTR	iDlgRet = 0;

	EC_D_DIALOG(DoModal());

	switch (iDlgRet)
	{
	case -1:
		DebugPrint(DBGGeneric,_T("Dialog box could not be created!\n"));
		MessageBox(_T("Dialog box could not be created!")); // STRING_OK
		return MAPI_E_CALL_FAILED;
		break;
	case IDABORT:
	case IDCANCEL:
		return MAPI_E_USER_CANCEL;
		break;
	case IDOK:
		return S_OK;
		break;
	default:
		return HRESULT_FROM_WIN32((unsigned long) iDlgRet);
		break;
	}
} // CEditor::DisplayDialog

// Computes good width and height in pixels
// Good is defined as big enough to display all elements at a minimum size, including title
SIZE CEditor::ComputeWorkArea(SIZE sScreen)
{
	SIZE sArea = {0};

	// Figure a good width (cx)
	int cx = 0;

	CRect OldRect;
	m_Prompt.GetRect(OldRect);
	// make the edit rect big so we can get an accurate line count
	m_Prompt.SetRectNP(CRect(0,0,MAX_WIDTH,sScreen.cy));

	int iPromptLineCount = m_Prompt.GetLineCount();

	CDC* dcSB = m_Prompt.GetDC();
	CFont* pFont = dcSB->SelectObject(m_Prompt.GetFont());

	CString strText;
	for (int i = 0; i<iPromptLineCount ; i++)
	{
		// length of line i:
		int len = m_Prompt.LineLength(m_Prompt.LineIndex(i));
		if (len)
		{
			m_Prompt.GetLine(i, strText.GetBuffer(len), len);
			strText.ReleaseBuffer(len);

			// this call fails miserably if we don't select a font above
			SIZE sizeText = dcSB->GetTabbedTextExtent(strText,0,0);
			cx = max(cx,sizeText.cx);
		}
	}

	ULONG j = 0;
	// width
	for (j = 0 ; j < m_cControls ; j++)
	{
		SIZE sizeText = {0};
		sizeText = dcSB->GetTabbedTextExtent(m_lpControls[j].szLabel,0,0);
		if (CTRL_CHECK == m_lpControls[j].ulCtrlType)
		{
			sizeText.cx += ::GetSystemMetrics(SM_CXMENUCHECK);
		}
		cx = max(cx,sizeText.cx);

		// Special check for strings in a drop down
		if (CTRL_DROPDOWN == m_lpControls[j].ulCtrlType)
		{
			int cxDropDown = 0;
			ULONG iDropString = 0;
			for (iDropString = 0; iDropString < m_lpControls[j].UI.lpDropDown->ulDropList; iDropString++)
			{
				CString szDropString;
				m_lpControls[j].UI.lpDropDown->DropDown.GetLBText(iDropString,szDropString);
				int cxDropString = dcSB->GetTextExtent(szDropString).cx;
				cxDropDown = max(cxDropDown, cxDropString);
			}

			// Add scroll bar
			cxDropDown += ::GetSystemMetrics(SM_CXVSCROLL);

			cx = max(cx,cxDropDown);
		}
	}

	dcSB->SelectObject(pFont);
	m_Prompt.ReleaseDC(dcSB);

	m_Prompt.SetRectNP(OldRect); // restore the old edit rectangle

	// Add internal margin for the edit controls - can't find a metric for this
	cx += 6;

	// cx now contains the width of the widest prompt string or control
	// Add a margin around that to frame our controls in the client area:
	cx += 2 * m_iMargin;

	// Insanely overly complicated code to determine how wide a title bar needs to be
	HRESULT hRes = S_OK;
	NONCLIENTMETRICS ncm = {0};

	// Get the system metrics.
	ncm.cbSize = sizeof(NONCLIENTMETRICS);
	EC_B(SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0));

	if (SUCCEEDED(hRes))
	{
		HDC myHDC = ::GetDC(NULL);
		HFONT titleFont = NULL;
		titleFont = ::CreateFontIndirect(&ncm.lfCaptionFont);
		HFONT hOldFont = (HFONT) ::SelectObject(myHDC,titleFont);
		int sizeTitle = NULL;
		EC_D(sizeTitle,LOWORD(GetTabbedTextExtent(myHDC,(LPCTSTR) m_szTitle, m_szTitle.GetLength(),0,0)));

		// Awesome reference here
		// http://shellrevealed.com/blogs/shellblog/archive/2006/10/12/Frequently-asked-questions-about-the-Aero-Basic-window-frame.aspx
		int iEdge = ::GetSystemMetrics(SM_CXEDGE);
		int iBtn  = ::GetSystemMetrics(SM_CXSIZE);
		int iIcon = ::GetSystemMetrics(SM_CXSMICON);
		// Non theme guess of how much space we need around the title text
		int iMargins = 5; // Confirmed on NT, where themes ('Visual Styles') don't exist

		int iSizeButtons = 0;
		iSizeButtons =
			// Testing with non themes shows there's some min width observed as the icon size goes down
			// This seems to capture it - I wish this stuff was documented somewhere
			max(iIcon,::GetSystemMetrics(SM_CXSMSIZE)) // application icon
			+ 3*(iBtn-iEdge) // min,max,close icons
			+ 3*iEdge // gaps between icons (beginning+end+between close and max)
			+ iMargins; // space before and after title text

		// If we have themes, we're gonna calculate our space a bit differently
		if (pfnOpenThemeData && pfnCloseThemeData && pfnGetThemeMargins)
		{
			HTHEME _hTheme = NULL;
			_hTheme = pfnOpenThemeData(NULL, L"Window"); // STRING_OK
			if (_hTheme)
			{
				// Concession to Vista Aero and it's funky buttons - don't wanna call WM_GETTITLEBARINFOEX here
				int iStupidFunkyAeroButtons = 3;

				// Different themes have different margins around the caption text - fetch it
				// No error case here - will default to the non-theme margins
				MARGINS marCaptionText = {0};
				if (SUCCEEDED(pfnGetThemeMargins(
					_hTheme,
					NULL, // myHDC,
					WP_CAPTION,
					CS_ACTIVE,
					TMT_CAPTIONMARGINS,
					NULL,
					&marCaptionText)))
				{
					iMargins = marCaptionText.cxLeftWidth + marCaptionText.cxRightWidth;
				}
				iSizeButtons =
					iIcon // application icon - themes are fine with this value no matter what the size
					+ 3*(iBtn-2*iEdge) // min,max,close icons
					+ 4*iEdge // gaps between icons (beginning+end+between close and max+between min and max)
					+ iMargins // space before and after title text
					+ iStupidFunkyAeroButtons;

				pfnCloseThemeData(_hTheme);
			}
		}

		sizeTitle += iSizeButtons;

		cx = max(cx,sizeTitle);
		::SelectObject(myHDC,hOldFont);
		if(titleFont) ::DeleteObject(titleFont);
	}

	// throw all that work out if we have enough buttons
	cx = max(cx,(int)(m_cButtons*m_iButtonWidth + m_iMargin*(m_cButtons+1)));
	// whatever cx we computed, bump it up if we need space for list buttons
	if (NOLIST != m_ulListNum) // Don't check bReadOnly - we want all list dialogs BIG
	{
		cx = max(cx,m_iButtonWidth*NUMLISTBUTTONS + m_iMargin*(NUMLISTBUTTONS+1));
	}
	// Done figuring a good width (cx)

	// Figure a good height (cy)
	int cy = 0;
	cy = 2 * m_iMargin; // margins top and bottom
	cy += iPromptLineCount * m_iTextHeight; // prompt text
	cy += m_iButtonHeight; // Button height
	cy += m_iMargin; // add a little height between the buttons and our edit controls

	m_cLabels = 0;
	m_cSingleLineBoxes = 0;
	m_cMultiLineBoxes = 0;
	m_cCheckBoxes = 0;
	m_cListBoxes = 0;
	m_cDropDowns = 0;
	for (j = 0 ; j < m_cControls ; j++)
	{
		if (m_lpControls[j].bUseLabelControl)
		{
			cy += m_iTextHeight; // count the label
			m_cLabels++;
		}
		switch (m_lpControls[j].ulCtrlType)
		{
		case CTRL_EDIT:
			if (m_lpControls[j].UI.lpEdit && m_lpControls[j].UI.lpEdit->bMultiline)
			{
				cy += 4 * m_iEditHeight; // four lines of text
				m_cMultiLineBoxes++;
			}
			else
			{
				cy += m_iEditHeight; // single line of text
				m_cSingleLineBoxes++;
			}
			break;
		case CTRL_CHECK:
			cy += m_iButtonHeight; // single line of text
			m_cCheckBoxes++;
			break;
		case CTRL_LIST:
			// TODO: Figure what a good base height here is
			cy += 4 * m_iEditHeight; // four lines of text
			if (!m_lpControls[j].bReadOnly)
			{
				cy +=  + m_iMargin + m_iButtonHeight; // plus some space and our buttons
			}
			m_cListBoxes++;
			break;
		case CTRL_DROPDOWN:
			cy += m_iEditHeight;
			m_cDropDowns++;
			break;
		}
	}
	// Done figuring a good height (cy)

	sArea.cx = cx;
	sArea.cy = cy;
	return sArea;
} // CEditor::ComputeWorkArea

void CEditor::OnSetDefaultSize()
{
	HRESULT hRes = S_OK;

	CRect rcMaxScreen;
	EC_B(SystemParametersInfo(SPI_GETWORKAREA,NULL,(LPVOID)(LPRECT)rcMaxScreen,NULL));
	int cxFullScreen = rcMaxScreen.Width();
	int cyFullScreen = rcMaxScreen.Height();

	SIZE sScreen = {cxFullScreen,cyFullScreen};
	SIZE sArea = ComputeWorkArea(sScreen);
	// inflate the rectangle according to the title bar, border, etc...
	CRect MyRect(0,0,sArea.cx,sArea.cy);
	// Add width and height for the nonclient frame - all previous calculations were done without it
	// This is a call to AdjustWindowRectEx
	CalcWindowRect(MyRect);

	if (MyRect.Width() > cxFullScreen || MyRect.Height() > cyFullScreen)
	{
		// small screen - tighten things up a bit and try again
		m_iMargin = 1;
		sArea = ComputeWorkArea(sScreen);
		MyRect.left = 0;
		MyRect.top = 0;
		MyRect.right = sArea.cx;
		MyRect.bottom = sArea.cy;
		CalcWindowRect(MyRect);
	}

	// worst case, don't go bigger than the screen
	m_iMinWidth = min(MyRect.Width(),cxFullScreen);
	m_iMinHeight = min(MyRect.Height(),cyFullScreen);

	// worst case v2, don't go bigger than MAX_WIDTH pixels wide
	m_iMinWidth = min(m_iMinWidth,MAX_WIDTH);

	BOOL bRet = 0;
	EC_D(bRet,SetWindowPos(
		NULL,
		0,
		0,
		m_iMinWidth,
		m_iMinHeight,
		SWP_NOZORDER | SWP_NOMOVE));
} // CEditor::OnSetDefaultSize

void CEditor::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	lpMMI->ptMinTrackSize.x = m_iMinWidth;
	lpMMI->ptMinTrackSize.y = m_iMinHeight;
} // CEditor::OnGetMinMaxInfo

LRESULT CEditor::OnNcHitTest(CPoint point)
{
	CRect gripRect;
	GetWindowRect(gripRect);
	gripRect.left = gripRect.right - ::GetSystemMetrics(SM_CXHSCROLL);
	gripRect.top  = gripRect.bottom - ::GetSystemMetrics(SM_CYVSCROLL);
    // Test to see if the cursor is within the 'gripper'
    // bitmap, and tell the system that the user is over
    // the lower right-hand corner if it is.
    if (gripRect.PtInRect(point))
    {
        return HTBOTTOMRIGHT ;
    }
    else
    {
        return CDialog::OnNcHitTest(point) ;
    }
} // CEditor::OnNcHitTest

void CEditor::OnSize(UINT nType, int cx, int cy)
{
	HRESULT hRes = S_OK;
	CDialog::OnSize(nType, cx, cy);

	int iFullWidth = cx - 2 * m_iMargin;

	int iPromptLineCount = m_Prompt.GetLineCount();

	int iCYBottom = cy - m_iButtonHeight - m_iMargin; // Top of Buttons
	int iCYTop = m_iTextHeight * iPromptLineCount + m_iMargin; // Bottom of prompt

	// Position prompt at top
	BOOL bRet = 0;
	EC_D(bRet,m_Prompt.SetWindowPos(
		0, // z-order
		m_iMargin, // new x
		m_iMargin, // new y
		iFullWidth, // Full width
		m_iTextHeight * iPromptLineCount,
		SWP_NOZORDER));

	if (m_cButtons)
	{
		int iSlotWidth = iFullWidth/m_cButtons;
		int iOffset = (iSlotWidth - m_iButtonWidth)/2;
		int iButton = 0;

		// Position buttons at the bottom, centered in respective slots
		if (m_bButtonFlags & CEDITOR_BUTTON_OK)
		{
			EC_D(bRet,m_OkButton.SetWindowPos(
				0,
				m_iMargin+iSlotWidth * iButton + iOffset, // new x
				iCYBottom, // new y
				min(m_iButtonWidth,iSlotWidth),
				m_iButtonHeight,
				SWP_NOZORDER));
			iButton++;
		}

		if (m_bButtonFlags & CEDITOR_BUTTON_ACTION1)
		{
			EC_D(bRet,m_ActionButton1.SetWindowPos(
				0,
				m_iMargin+iSlotWidth * iButton + iOffset, // new x
				iCYBottom, // new y
				min(m_iButtonWidth,iSlotWidth),
				m_iButtonHeight,
				SWP_NOZORDER));
			iButton++;
		}

		if (m_bButtonFlags & CEDITOR_BUTTON_ACTION2)
		{
			EC_D(bRet,m_ActionButton2.SetWindowPos(
				0,
				m_iMargin+iSlotWidth * iButton + iOffset, // new x
				iCYBottom, // new y
				min(m_iButtonWidth,iSlotWidth),
				m_iButtonHeight,
				SWP_NOZORDER));
			iButton++;
		}

		if (m_bButtonFlags & CEDITOR_BUTTON_CANCEL)
		{
			EC_D(bRet,m_CancelButton.SetWindowPos(
				0,
				m_iMargin+iSlotWidth * iButton + iOffset, // new x
				iCYBottom, // new y
				min(m_iButtonWidth,iSlotWidth),
				m_iButtonHeight,
				SWP_NOZORDER));
			iButton++;
		}
	}

	iCYBottom -= m_iMargin; // add a margin above the buttons
	// at this point, iCYTop and iCYBottom reflect our free space, so we can calc multiline height

	int iMultiHeight = 0;
	int iListHeight = 0;
	if (m_cMultiLineBoxes+m_cListBoxes)
	{
		iMultiHeight = ((iCYBottom - iCYTop) // total space available
			- m_cLabels*m_iTextHeight // all labels
			- m_cSingleLineBoxes*m_iEditHeight // singleline edit boxes
			- m_cCheckBoxes*m_iButtonHeight // checkboxes
			- m_cDropDowns*m_iEditHeight // dropdown combo boxes
			) // minus the occupied space
			/(m_cMultiLineBoxes + m_cListBoxes); // and divided by the number of needed partitions
		iListHeight = iMultiHeight - m_iButtonHeight - m_iMargin;
	}


	for (ULONG j = 0 ; j < m_cControls ; j++)
	{
		if (m_lpControls[j].bUseLabelControl)
		{
			EC_D(bRet,m_lpControls[j].Label.SetWindowPos(
				0,
				m_iMargin, // new x
				iCYTop, // new y
				iFullWidth, // new width
				m_iTextHeight, // new height
				SWP_NOZORDER));
			iCYTop += m_iTextHeight;
		}
		switch (m_lpControls[j].ulCtrlType)
		{
		case CTRL_EDIT:
			if (m_lpControls[j].UI.lpEdit)
			{
				int iViewHeight = 0;

				if (m_lpControls[j].UI.lpEdit->bMultiline)
				{
					iViewHeight = iMultiHeight;
				}
				else
				{
					iViewHeight = m_iEditHeight;
				}
				EC_D(bRet,m_lpControls[j].UI.lpEdit->EditBox.SetWindowPos(
					0,
					m_iMargin, // new x
					iCYTop, // new y
					iFullWidth, // new width
					iViewHeight, // new height
					SWP_NOZORDER));

				iCYTop += iViewHeight;
			}

			break;
		case CTRL_CHECK:
			if (m_lpControls[j].UI.lpCheck)
			{
				EC_D(bRet,m_lpControls[j].UI.lpCheck->Check.SetWindowPos(
					0,
					m_iMargin, // new x
					iCYTop, // new y
					iFullWidth, // new width
					m_iButtonHeight, // new height
					SWP_NOZORDER));
				iCYTop += m_iButtonHeight;
			}
			break;
		case CTRL_LIST:
			if (m_lpControls[j].UI.lpList)
			{
				EC_D(bRet,m_lpControls[j].UI.lpList->List.SetWindowPos(
					0,
					m_iMargin, // new x
					iCYTop, // new y
					iFullWidth, // new width
					iListHeight, // new height
					SWP_NOZORDER));
				iCYTop += iListHeight;

				if (!m_lpControls[j].bReadOnly)
				{
					// buttons go below the list:
					iCYTop += m_iMargin;
					if (NUMLISTBUTTONS)
					{
						int iSlotWidth = iFullWidth/NUMLISTBUTTONS;
						int iOffset = (iSlotWidth - m_iButtonWidth)/2;
						int iButton = 0;

						for (iButton = 0 ; iButton < NUMLISTBUTTONS ; iButton++)
						{
							EC_D(bRet,m_lpControls[j].UI.lpList->ButtonArray[iButton].SetWindowPos(
								0,
								m_iMargin+iSlotWidth * iButton + iOffset,
								iCYTop, // new y
								m_iButtonWidth,
								m_iButtonHeight,
								SWP_NOZORDER));
						}

						iCYTop += m_iButtonHeight;
					}
				}
			}
			break;
		case CTRL_DROPDOWN:
			if (m_lpControls[j].UI.lpDropDown)
			{
				// Note - Real height of a combo box is fixed at m_iEditHeight
				// Height we set here influences the amount of dropdown entries we see
				// Only really matters on Win2k and below.
				ULONG ulDrops = 1+ min(m_lpControls[j].UI.lpDropDown->ulDropList,4);

				EC_D(bRet,m_lpControls[j].UI.lpDropDown->DropDown.SetWindowPos(
					0,
					m_iMargin, // new x
					iCYTop, // new y
					iFullWidth, // new width
					m_iEditHeight*(ulDrops), // new height
					SWP_NOZORDER));

				iCYTop += m_iEditHeight;
			}
			break;
		}
	}

	EC_B(RedrawWindow()); // not sure why I have to call this...doesn't SetWindowPos force the refresh?
} // CEditor::OnSize

void CEditor::CreateControls(ULONG ulCount)
{
	if (m_lpControls) DeleteControls();
	m_cControls = ulCount;
	if (m_cControls > 0)
	{
		m_lpControls = new ControlStruct[m_cControls];
	}
	if (m_lpControls)
	{
		for (ULONG i = 0 ; i < m_cControls ; i++)
		{
			m_lpControls[i].ulCtrlType = CTRL_EDIT;
			m_lpControls[i].UI.lpEdit = NULL;
		}
	}
} // CEditor::CreateControls

void CEditor::DeleteControls()
{
	if (m_lpControls)
	{
		for (ULONG i = 0 ; i < m_cControls ; i++)
		{
			switch (m_lpControls[i].ulCtrlType)
			{
			case CTRL_EDIT:
				if (m_lpControls[i].UI.lpEdit)
				{
					ClearString(i);
					delete m_lpControls[i].UI.lpEdit;
				}
				break;
			case CTRL_CHECK:
				delete m_lpControls[i].UI.lpCheck;
				break;
			case CTRL_LIST:
				delete m_lpControls[i].UI.lpList;
				break;
			case CTRL_DROPDOWN:
				delete m_lpControls[i].UI.lpDropDown;
				break;
			}
		}
		delete[] m_lpControls;
	}
	m_lpControls = NULL;
	m_cControls = 0;
} // CEditor::DeleteControls

void CEditor::SetPromptPostFix(LPCTSTR szMsg)
{
	m_szPromptPostFix = szMsg;
} // CEditor::SetPromptPostFix

// Updates the specified edit box - does not trigger change notifications
// Copies string from m_lpControls[i].UI.lpEdit->lpszW
// Only function that modifies the text in the edit controls
void CEditor::SetEditBoxText(ULONG i)
{
	if (!IsValidEdit(i)) return;
	if (!m_lpControls[i].UI.lpEdit->EditBox.m_hWnd) return;

	ULONG ulEventMask = m_lpControls[i].UI.lpEdit->EditBox.GetEventMask(); // Get original mask
	m_lpControls[i].UI.lpEdit->EditBox.SetEventMask(ENM_NONE);
	SETTEXTEX setText = {0};

	setText.flags = ST_DEFAULT;
	setText.codepage = 1200;

	LRESULT lRes = NULL;

	lRes = ::SendMessage(
		m_lpControls[i].UI.lpEdit->EditBox.m_hWnd,
		EM_SETTEXTEX,
		(WPARAM) &setText,
		(LPARAM) m_lpControls[i].UI.lpEdit->lpszW);

	if (0 == lRes)
	{
		// We failed to set text like we wanted, fall back to WM_SETTEXT
		HRESULT hRes = S_OK;
		LPSTR szA = NULL;
		EC_H(UnicodeToAnsi(m_lpControls[i].UI.lpEdit->lpszW,&szA));
		lRes = ::SendMessage(
			m_lpControls[i].UI.lpEdit->EditBox.m_hWnd,
			WM_SETTEXT,
			(WPARAM) NULL,
			(LPARAM) szA);
		delete[] szA;
	}

	m_lpControls[i].UI.lpEdit->EditBox.SetEventMask(ulEventMask); // put original mask back
} // CEditor::SetEditBoxText

// Clears the strings out of an lpEdit
void CEditor::ClearString(ULONG i)
{
	if (!IsValidEdit(i)) return;

	delete[] m_lpControls[i].UI.lpEdit->lpszW;
	delete[] m_lpControls[i].UI.lpEdit->lpszA;
	m_lpControls[i].UI.lpEdit->lpszW = NULL;
	m_lpControls[i].UI.lpEdit->lpszA = NULL;
} // CEditor::ClearString

// Sets m_lpControls[i].UI.lpEdit->lpszW
void CEditor::SetStringA(ULONG i,LPCSTR szMsg)
{
	if (!IsValidEdit(i)) return;

	if (!szMsg) szMsg = "";
	HRESULT hRes = S_OK;

	LPWSTR szMsgW = NULL;
	EC_H(AnsiToUnicode(szMsg,&szMsgW));
	if (SUCCEEDED(hRes))
	{
		SetStringW(i,szMsgW);
	}
	delete[] szMsgW;
} // CEditor::SetStringA

// Sets m_lpControls[i].UI.lpEdit->lpszW
void CEditor::SetStringW(ULONG i,LPCWSTR szMsg)
{
	if (!IsValidEdit(i)) return;

	ClearString(i);

	if (!szMsg) szMsg = L"";
	HRESULT hRes = S_OK;
	size_t cchszW = 0;

	EC_H(StringCchLengthW(szMsg,STRSAFE_MAX_CCH,&cchszW));
	cchszW++;
	m_lpControls[i].UI.lpEdit->lpszW = new WCHAR[cchszW];

	if (m_lpControls[i].UI.lpEdit->lpszW)
	{
		EC_H(StringCchCopyW(
			m_lpControls[i].UI.lpEdit->lpszW,
			cchszW,
			szMsg));
		if (FAILED(hRes))
		{
			ClearString(i);
		}
	}

	SetEditBoxText(i);
} // CEditor::SetStringW

// Updates m_lpControls[i].UI.lpEdit->lpszW
void __cdecl CEditor::SetStringf(ULONG i,LPCTSTR szMsg,...)
{
	if (!IsValidEdit(i)) return;

	CString szTemp;

	if (szMsg)
	{
		va_list argList = NULL;
		va_start(argList, szMsg);
		szTemp.FormatV(szMsg,argList);
		va_end(argList);
	}

	SetString(i,(LPCTSTR) szTemp);
} // CEditor::SetStringf

void CEditor::LoadString(ULONG i, UINT uidMsg)
{
	if (!IsValidEdit(i)) return;

	CString szTemp;

	if (uidMsg)
	{
		szTemp.LoadString(uidMsg);
	}

	SetString(i,(LPCTSTR) szTemp);
} // CEditor::LoadString

void CEditor::SetBinary(ULONG i,LPBYTE lpb, size_t cb)
{
	LPTSTR lpszStr = NULL;
	MyHexFromBin(
		lpb,
		cb,
		&lpszStr);

	// If lpszStr happens to be NULL, SetString will deal with it
	SetString(i,lpszStr);
	delete[] lpszStr;
} // CEditor::SetBinary

void CEditor::SetSize(ULONG i, size_t cb)
{
	SetStringf(i,_T("0x%08X = %d"), cb, cb); // STRING_OK
} // CEditor::SetSize

// returns false if we failed to get a binary
BOOL CEditor::GetBinaryUseControl(ULONG i,size_t* cbBin,LPBYTE* lpBin)
{
	if (!IsValidEdit(i)) return false;
	if (!cbBin || ! lpBin) return false;
	CString szString;

	*cbBin = NULL;
	*lpBin = NULL;

	szString = GetStringUseControl(i);

	// remove any whitespace before decoding
	szString.Replace(_T("\r"),_T("")); // STRING_OK
	szString.Replace(_T("\n"),_T("")); // STRING_OK
	szString.Replace(_T("\t"),_T("")); // STRING_OK
	szString.Replace(_T(" "),_T("")); // STRING_OK

	size_t cchStrLen = szString.GetLength();

	if (cchStrLen & 1) return false; // odd length strings aren't valid at all

	*cbBin = cchStrLen / 2;
	*lpBin = new BYTE[*cbBin+2]; // lil extra space to shove a NULL terminator on there
	if (*lpBin)
	{
		MyBinFromHex(
			(LPCTSTR) szString,
			*lpBin,
			*cbBin);
		// In case we try printing this...
		(*lpBin)[*cbBin] = 0;
		(*lpBin)[*cbBin+1] = 0;
		return true;
	}
	else *cbBin = NULL;
	return false;
} // CEditor::GetBinaryUseControl

BOOL CEditor::GetCheckUseControl(ULONG iControl)
{
	if (!IsValidCheck(iControl)) return false;

	return m_lpControls[iControl].UI.lpCheck->Check.GetCheck();
} // CEditor::GetCheckUseControl

// converts string in a text(edit) control into an entry ID
// Can base64 decode if needed
// entryID is allocated with new, free with delete[]
HRESULT CEditor::GetEntryID(ULONG i, BOOL bIsBase64, size_t* cbBin, LPENTRYID* lppEID)
{
	if (!IsValidEdit(i)) return MAPI_E_INVALID_PARAMETER;
	if (!cbBin || !lppEID) return MAPI_E_INVALID_PARAMETER;

	*cbBin = NULL;
	*lppEID = NULL;

	HRESULT	hRes = S_OK;
	LPCTSTR szString = GetString(i);

	if (szString)
	{
		size_t	cchString = NULL;

		EC_H(StringCchLength(szString,STRSAFE_MAX_CCH,&cchString));

		if (FAILED(hRes) || cchString & 1) return hRes; // can't use an odd length string - Hex strings are multiples of two and Base64 are multiples of 4

		if (bIsBase64) // entry was BASE64 encoded
		{
			EC_H(Base64Decode(szString,cbBin,(LPBYTE*) lppEID));
		}
		else // Entry was hexized string
		{
			*cbBin = cchString / 2;

			if (0 != *cbBin)
			{
				*lppEID = (LPENTRYID) new BYTE[*cbBin];
				if (*lppEID)
				{
					MyBinFromHex(
						szString,
						(LPBYTE) *lppEID,
						*cbBin);
				}
			}
		}
	}

	return hRes;
} // CEditor::GetEntryID

void CEditor::SetHex(ULONG i, ULONG ulVal)
{
	SetStringf(i,_T("0x%08X"),ulVal); // STRING_OK
} // CEditor::SetHex

void CEditor::SetDecimal(ULONG i, ULONG ulVal)
{
	SetStringf(i,_T("%d"),ulVal); // STRING_OK
} // CEditor::SetDecimal

void CEditor::SetListString(ULONG iControl, ULONG iListRow, ULONG iListCol, LPCTSTR szListString)
{
	if (!IsValidList(iControl)) return;

	HRESULT hRes = S_OK;
	WC_B(m_lpControls[iControl].UI.lpList->List.SetItemText(iListRow,iListCol,szListString?szListString:_T("")));
} // CEditor::SetListString

SortListData* CEditor::InsertListRow(ULONG iControl, int iRow, LPCTSTR szText)
{
	if (!IsValidList(iControl)) return NULL;

	return m_lpControls[iControl].UI.lpList->List.InsertRow(iRow,(LPTSTR) szText);
} // CEditor::InsertListRow

void CEditor::InsertDropString(ULONG iControl, int iRow, LPCTSTR szText)
{
	if (!IsValidDropDown(iControl)) return;

	m_lpControls[iControl].UI.lpDropDown->DropDown.InsertString(iRow,szText);
	m_lpControls[iControl].UI.lpDropDown->ulDropList++;
} // CEditor::InsertDropString

void CEditor::SetEditReadOnly(ULONG iControl)
{
	if (!IsValidEdit(iControl)) return;

	m_lpControls[iControl].UI.lpEdit->EditBox.SetBackgroundColor(false,GetSysColor(COLOR_BTNFACE));
	m_lpControls[iControl].UI.lpEdit->EditBox.SetReadOnly();
} // CEditor::SetEditReadOnly

void CEditor::ClearList(ULONG iControl)
{
	if (!IsValidList(iControl)) return;

	HRESULT hRes = S_OK;

	m_lpControls[iControl].UI.lpList->List.DeleteAllColumns();
	EC_B(m_lpControls[iControl].UI.lpList->List.DeleteAllItems());
} // CEditor::ClearList

void CEditor::ResizeList(ULONG iControl, BOOL bSort)
{
	if (!IsValidList(iControl)) return;

	if (bSort)
	{
		m_lpControls[iControl].UI.lpList->List.SortClickedColumn();
	}

	m_lpControls[iControl].UI.lpList->List.AutoSizeColumns();
} // CEditor::ResizeList

LPWSTR CEditor::GetStringW(ULONG i)
{
	if (!IsValidEdit(i)) return NULL;

	return m_lpControls[i].UI.lpEdit->lpszW;
} // CEditor::GetStringW

LPSTR CEditor::GetStringA(ULONG i)
{
	if (!IsValidEdit(i)) return NULL;

	HRESULT hRes = S_OK;
	// Don't use ClearString - that would wipe the lpszW too!
	delete[] m_lpControls[i].UI.lpEdit->lpszA;
	m_lpControls[i].UI.lpEdit->lpszA = NULL;

	// We're not leaking this conversion
	// It goes into m_lpControls[i].UI.lpEdit->lpszA, which we manage
	EC_H(UnicodeToAnsi(m_lpControls[i].UI.lpEdit->lpszW,&m_lpControls[i].UI.lpEdit->lpszA));

	return m_lpControls[i].UI.lpEdit->lpszA;
} // CEditor::GetStringA

// Gets string from edit box and places it in m_lpControls[i].UI.lpEdit->lpszW
void CEditor::GetEditBoxText(ULONG i)
{
	if (!IsValidEdit(i)) return;

	ClearString(i);

	GETTEXTLENGTHEX getTextLength = {0};
	getTextLength.flags = GTL_PRECISE | GTL_NUMBYTES;
	getTextLength.codepage = 1200;

	size_t cchText = 0;

	cchText = (size_t)::SendMessage(
		m_lpControls[i].UI.lpEdit->EditBox.m_hWnd,
		EM_GETTEXTLENGTHEX,
		(WPARAM) &getTextLength,
		(LPARAM) 0);

	if (0 == cchText || E_INVALIDARG == cchText)
	{
		// we didn't get a length - try another method
		cchText = (size_t)::SendMessage(
			m_lpControls[i].UI.lpEdit->EditBox.m_hWnd,
			WM_GETTEXTLENGTH,
			(WPARAM) 0,
			(LPARAM) 0);
	}

	// Our EM_GETTEXTLENGTHEX call is asking for number of bytes (GTL_NUMBYTES).
	// Documentation is unclear whether this will or will not include 2 bytes for the NULL terminator
	// (though in testing it always does)
	// WM_GETTEXTLENGTH does not include the NULL terminator
	// So adding 1 to be safe - want this to always be positive
	cchText += 1;

	LPWSTR lpszW = (WCHAR*) new WCHAR[cchText];
	size_t cchW = 0;

	if (lpszW)
	{
		memset(lpszW,0,cchText*sizeof(WCHAR));

		if (cchText > 1) // No point in checking if the string is just a null terminator
		{
			GETTEXTEX getText = {0};
			getText.cb = (DWORD) cchText*sizeof(WCHAR);
			getText.flags = GT_DEFAULT;
			getText.codepage = 1200;

			cchW = ::SendMessage(
				m_lpControls[i].UI.lpEdit->EditBox.m_hWnd,
				EM_GETTEXTEX,
				(WPARAM) &getText,
				(LPARAM) lpszW);
			if (0 == cchW)
			{
				// Didn't get a string from this message, fall back to WM_GETTEXT
				LPSTR lpszA = (CHAR*) new CHAR[cchText];
				if (lpszA)
				{
					memset(lpszA,0,cchText*sizeof(CHAR));
					HRESULT hRes = S_OK;
					cchW = ::SendMessage(
						m_lpControls[i].UI.lpEdit->EditBox.m_hWnd,
						WM_GETTEXT,
						(WPARAM) cchText,
						(LPARAM) lpszA);
					if (0 != cchW)
					{
						EC_H(StringCchPrintfW(lpszW,cchText,L"%hs",lpszA)); // STRING_OK
					}
					delete[] lpszA;
				}
			}
		}

		m_lpControls[i].UI.lpEdit->lpszW = lpszW;
	}
} // CEditor::GetEditBoxValue

// No need to free this - treat it like a static
LPSTR CEditor::GetEditBoxTextA(ULONG i)
{
	if (!IsValidEdit(i)) return NULL;

	GetEditBoxText(i);

	return GetStringA(i);
} // CEditor::GetEditBoxTextA

LPWSTR CEditor::GetEditBoxTextW(ULONG iControl)
{
	if (!IsValidEdit(iControl)) return NULL;

	GetEditBoxText(iControl);

	return GetStringW(iControl);
} // CEditor::GetEditBoxTextW

void CEditor::GetEditBoxStream(ULONG iControl, LPSTREAM lpStreamOut, BOOL bUnicode, BOOL bRTF)
{
	EDITSTREAM	es = {0, 0, EditStreamWriteCallBack};
	LONG		cb = 0;
	UINT		uFormat = SF_TEXT;

	// If we're unicode, we need to pass this flag to use Unicode RichEdit
	// However, PR_RTF_COMPRESSED don't play Unicode, so we check if we're RTF first
	if (bUnicode && !bRTF)
	{
		uFormat |= SF_UNICODE;
	}

	es.dwCookie	= (DWORD_PTR)lpStreamOut;

	cb = m_lpControls[iControl].UI.lpEdit->EditBox.StreamOut(uFormat,es);
	DebugPrintEx(DBGGeneric,CLASS,_T("CEditor::GetEditBoxStream"),_T("wrote 0x%X\n"),cb);
} // CEditor::GetEditBoxStream

ULONG CEditor::GetHex(ULONG i)
{
	if (!IsValidEdit(i)) return 0;

	return wcstoul(m_lpControls[i].UI.lpEdit->lpszW,NULL,16);
} // CEditor::GetHex

CString	CEditor::GetStringUseControl(ULONG iControl)
{
	if (!IsValidEdit(iControl)) return _T("");

	CString szText;
	m_lpControls[iControl].UI.lpEdit->EditBox.GetWindowText(szText);

	return szText;
} // CEditor::GetStringUseControl

CString	CEditor::GetDropStringUseControl(ULONG iControl)
{
	if (!IsValidDropDown(iControl)) return _T("");

	CString szText;
	m_lpControls[iControl].UI.lpDropDown->DropDown.GetWindowText(szText);

	return szText;
} // CEditor::GetDropStringUseControl

int CEditor::GetDropDownSelection(ULONG iControl)
{
	if (!IsValidDropDown(iControl)) return 0;

	return m_lpControls[iControl].UI.lpDropDown->DropDown.GetCurSel();
} // CEditor::GetDropDownSelection

ULONG CEditor::GetListCount(ULONG iControl)
{
	if (!IsValidList(iControl)) return 0;

	return m_lpControls[iControl].UI.lpList->List.GetItemCount();
} // CEditor::GetListCount

SortListData* CEditor::GetListRowData(ULONG iControl, int iRow)
{
	if (!IsValidList(iControl)) return 0;

	return (SortListData*) m_lpControls[iControl].UI.lpList->List.GetItemData(iRow);
} // CEditor::GetListRowData

SortListData* CEditor::GetSelectedListRowData(ULONG iControl)
{
	if (!IsValidList(iControl)) return NULL;

	int	iItem = m_lpControls[iControl].UI.lpList->List.GetNextItem(-1,LVNI_FOCUSED | LVNI_SELECTED);

	if (-1 != iItem)
	{
		return GetListRowData(iControl,iItem);
	}
	return NULL;
} // CEditor::GetSelectedListRowData

BOOL CEditor::ListDirty(ULONG iControl)
{
	if (!IsValidList(iControl)) return 0;

	return m_lpControls[iControl].UI.lpList->bDirty;
} // CEditor::ListDirty

BOOL CEditor::EditDirty(ULONG iControl)
{
	if (!IsValidEdit(iControl)) return 0;

	return m_lpControls[iControl].UI.lpEdit->EditBox.GetModify();
} // CEditor::EditDirty

ULONG CEditor::GetHexUseControl(ULONG i)
{
	if (!IsValidEdit(i)) return 0;

	CString szTmpString = GetStringUseControl(i);

	return _tcstoul((LPCTSTR) szTmpString,NULL,16);
} // CEditor::GetHexUseControl

ULONG CEditor::GetDecimal(ULONG i)
{
	if (!IsValidEdit(i)) return 0;

	return wcstoul(m_lpControls[i].UI.lpEdit->lpszW,NULL,10);
} // CEditor::GetDecimal

BOOL CEditor::GetCheck(ULONG i)
{
	if (!IsValidCheck(i)) return false;

	return m_lpControls[i].UI.lpCheck->bCheckValue;
} // CEditor::GetCheck

void CEditor::SetDropDownSelection(ULONG i,LPCTSTR szText)
{
	if (!IsValidDropDown(i)) return;

	HRESULT hRes = S_OK;

	int iSelect = m_lpControls[i].UI.lpDropDown->DropDown.SelectString(0,szText);

	// if we can't select, try pushing the text in there
	// not all dropdowns will support this!
	if (CB_ERR == iSelect)
	{
		EC_B(::SendMessage(
			m_lpControls[i].UI.lpDropDown->DropDown.m_hWnd,
			WM_SETTEXT,
			NULL,
			(LPARAM) szText));
	}
} // CEditor::SetDropDownSelection

int	CEditor::GetDropDown(ULONG i)
{
	if (!IsValidDropDown(i)) return -1;

	return m_lpControls[i].UI.lpDropDown->iDropValue;
} // CEditor::GetDropDown

void CEditor::InitMultiLine(ULONG i, UINT uidLabel, LPCTSTR szVal, BOOL bReadOnly)
{
	if (INVALIDRANGE(i)) return;
	InitSingleLineSz(i,uidLabel,szVal,bReadOnly);
	m_lpControls[i].UI.lpEdit->bMultiline = true;
} // CEditor::InitMultiLine

void CEditor::InitSingleLineSz(ULONG i, UINT uidLabel, LPCTSTR szVal, BOOL bReadOnly)
{
	if (INVALIDRANGE(i)) return;

	m_lpControls[i].ulCtrlType = CTRL_EDIT;
	m_lpControls[i].bReadOnly = bReadOnly;
	m_lpControls[i].nID = 0;
	m_lpControls[i].bUseLabelControl = true;
	m_lpControls[i].uidLabel = uidLabel;
	m_lpControls[i].UI.lpEdit = new EditStruct;
	if (m_lpControls[i].UI.lpEdit)
	{
		m_lpControls[i].UI.lpEdit->bMultiline = false;
		m_lpControls[i].UI.lpEdit->lpszW = 0;
		m_lpControls[i].UI.lpEdit->lpszA = 0;
		SetString(i,szVal);
	}
} // CEditor::InitSingleLineSz

void CEditor::InitSingleLine(ULONG i, UINT uidLabel, UINT uidVal, BOOL bReadOnly)
{
	if (INVALIDRANGE(i)) return;

	InitSingleLineSz(i,uidLabel,NULL,bReadOnly);

	LoadString(i,uidVal);
} // CEditor::InitSingleLine

void CEditor::InitCheck(ULONG i, UINT uidLabel, BOOL bVal, BOOL bReadOnly)
{
	if (INVALIDRANGE(i)) return;

	m_lpControls[i].ulCtrlType = CTRL_CHECK;
	m_lpControls[i].bReadOnly = bReadOnly;
	m_lpControls[i].nID = 0;
	m_lpControls[i].bUseLabelControl = false;
	m_lpControls[i].uidLabel = uidLabel;
	m_lpControls[i].UI.lpCheck = new CheckStruct;
	if (m_lpControls[i].UI.lpCheck)
	{
		m_lpControls[i].UI.lpCheck->bCheckValue = bVal;
	}
} // CEditor::InitCheck

void CEditor::InitList(ULONG i, UINT uidLabel, BOOL bAllowSort, BOOL bReadOnly)
{
	if (INVALIDRANGE(i)) return;
	if (NOLIST != m_ulListNum) return; // can't create a second list right now
	m_ulListNum = i;

	m_lpControls[i].ulCtrlType = CTRL_LIST;
	m_lpControls[i].bReadOnly = bReadOnly;
	m_lpControls[i].nID = 0;
	m_lpControls[i].bUseLabelControl = true;
	m_lpControls[i].uidLabel = uidLabel;
	m_lpControls[i].UI.lpList = new ListStruct;
	if (m_lpControls[i].UI.lpList)
	{
		m_lpControls[i].UI.lpList->bDirty = false;
		m_lpControls[i].UI.lpList->bAllowSort = bAllowSort;
	}
} // CEditor::InitList

void CEditor::InitDropDown(ULONG i, UINT uidLabel, ULONG ulDropList, UINT* lpuidDropList, BOOL bReadOnly)
{
	if (INVALIDRANGE(i)) return;

	m_lpControls[i].ulCtrlType = CTRL_DROPDOWN;
	m_lpControls[i].bReadOnly = bReadOnly;
	m_lpControls[i].nID = 0;
	m_lpControls[i].bUseLabelControl = true;
	m_lpControls[i].uidLabel = uidLabel;
	m_lpControls[i].UI.lpDropDown = new DropDownStruct;
	if (m_lpControls[i].UI.lpDropDown)
	{
		m_lpControls[i].UI.lpDropDown->ulDropList = ulDropList;
		m_lpControls[i].UI.lpDropDown->lpuidDropList = lpuidDropList;
		m_lpControls[i].UI.lpDropDown->iDropValue = CB_ERR;
	}
} // CEditor::InitDropDown

void CEditor::InitEditFromStream(ULONG iControl, LPSTREAM lpStreamIn, BOOL bUnicode, BOOL bRTF)
{
	if (!IsValidEdit(iControl)) return;

	EDITSTREAM	es = {0, 0, EditStreamReadCallBack};
	UINT		uFormat = SF_TEXT;

	// If we're unicode, we need to pass this flag to use Unicode RichEdit
	// However, PR_RTF_COMPRESSED don't play Unicode, so we check if we're RTF first
	if (bUnicode && !bRTF)
	{
		uFormat |= SF_UNICODE;
	}

	long lBytesRead = 0;

	es.dwCookie = (DWORD_PTR)lpStreamIn;

	// read the 'text' stream into control
	lBytesRead = m_lpControls[iControl].UI.lpEdit->EditBox.StreamIn(uFormat,es);
	DebugPrintEx(DBGGeneric,CLASS,_T("CEditor::InitEditFromStream"),_T("read %d bytes from the stream\n"),lBytesRead);

	// Clear the modify bit so this stream appears untouched
	m_lpControls[iControl].UI.lpEdit->EditBox.SetModify(false);
} // CEditor::InitEditFromStream

void CEditor::InsertColumn(ULONG ulListNum,int nCol,UINT uidText)
{
	if (!IsValidList(ulListNum)) return;
	CString szText;
	szText.LoadString(uidText);

	m_lpControls[ulListNum].UI.lpList->List.InsertColumn(nCol,szText);
} // CEditor::InsertColumn

ULONG CEditor::HandleChange(UINT nID)
{
	if (!m_lpControls) return (ULONG) -1;
	ULONG i = 0;
	BOOL bHandleMatched = false;
	for (i = 0 ; i < m_cControls ; i++)
	{
		if (m_lpControls[i].nID == nID)
		{
			bHandleMatched = true;
			break;
		}
	}
	if (!bHandleMatched) return (ULONG) -1;

	if (CTRL_EDIT == m_lpControls[i].ulCtrlType)
	{
		m_lpControls[i].UI.lpEdit->EditBox.SetFont(GetFont());
	}
	return i;
} // CEditor::HandleChange

// draw the grippy thing
void CEditor::OnPaint()
{
	HRESULT hRes = S_OK;
	PAINTSTRUCT ps;
	CDC* dc = NULL;
	EC_D(dc,BeginPaint(&ps));

	if(dc && !IsZoomed())
	{
		CRect rc;
		GetClientRect(&rc);

		rc.left = rc.right - ::GetSystemMetrics(SM_CXHSCROLL);
		rc.top = rc.bottom - ::GetSystemMetrics(SM_CYVSCROLL);

		dc->DrawFrameControl(rc, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
	}
	EndPaint(&ps);
} // CEditor::OnPaint

BOOL CEditor::IsValidDropDown(ULONG ulNum)
{
	if (INVALIDRANGE(ulNum)) return false;
	if (!m_lpControls) return false;
	if (CTRL_DROPDOWN != m_lpControls[ulNum].ulCtrlType) return false;
	if (!m_lpControls[ulNum].UI.lpDropDown) return false;
	return true;
} // CEditor::IsValidDropDown

BOOL CEditor::IsValidEdit(ULONG ulNum)
{
	if (INVALIDRANGE(ulNum)) return false;
	if (!m_lpControls) return false;
	if (CTRL_EDIT != m_lpControls[ulNum].ulCtrlType) return false;
	if (!m_lpControls[ulNum].UI.lpEdit) return false;
	return true;
} // CEditor::IsValidEdit

BOOL CEditor::IsValidList(ULONG ulNum)
{
	if (NOLIST == ulNum) return false;
	if (INVALIDRANGE(ulNum)) return false;
	if (!m_lpControls) return false;
	if (CTRL_LIST != m_lpControls[ulNum].ulCtrlType) return false;
	if (!m_lpControls[ulNum].UI.lpList) return false;
	return true;
} // CEditor::IsValidList

BOOL CEditor::IsValidListWithButtons(ULONG ulNum)
{
	if (!IsValidList(ulNum)) return false;
	if (m_lpControls[ulNum].bReadOnly) return false;
	return true;
} // CEditor::IsValidListWithButtons

BOOL CEditor::IsValidCheck(ULONG iControl)
{
	if (INVALIDRANGE(iControl)) return false;
	if (!m_lpControls) return false;
	if (CTRL_CHECK != m_lpControls[iControl].ulCtrlType) return false;
	if (!m_lpControls[iControl].UI.lpCheck) return false;
	return true;
} // CEditor::IsValidCheck

void CEditor::UpdateListButtons()
{
	if (!IsValidListWithButtons(m_ulListNum)) return;

	ULONG ulNumItems = m_lpControls[m_ulListNum].UI.lpList->List.GetItemCount();

	HRESULT hRes = S_OK;
	int iButton = 0;

	for (iButton = 0; iButton <NUMLISTBUTTONS; iButton++)
	{
		switch(ListButtons[iButton].uiButtonID)
		{
		case IDD_LISTMOVETOBOTTOM:
		case IDD_LISTMOVEDOWN:
		case IDD_LISTMOVETOTOP:
		case IDD_LISTMOVEUP:
			{
				if (ulNumItems >= 2)
					EC_B(m_lpControls[m_ulListNum].UI.lpList->ButtonArray[iButton].EnableWindow(true))
				else
					EC_B(m_lpControls[m_ulListNum].UI.lpList->ButtonArray[iButton].EnableWindow(false));
				break;
			}
		case IDD_LISTDELETE:
			{
				if (ulNumItems >= 1)
					EC_B(m_lpControls[m_ulListNum].UI.lpList->ButtonArray[iButton].EnableWindow(true))
				else
					EC_B(m_lpControls[m_ulListNum].UI.lpList->ButtonArray[iButton].EnableWindow(false));
				break;
			}
		case IDD_LISTEDIT:
			{
				if (ulNumItems >= 1)
					EC_B(m_lpControls[m_ulListNum].UI.lpList->ButtonArray[iButton].EnableWindow(true))
				else
					EC_B(m_lpControls[m_ulListNum].UI.lpList->ButtonArray[iButton].EnableWindow(false));
				break;
			}
		case IDD_LISTADD:
			{
				break;
			}
		}
	}
} // CEditor::UpdateListButtons

void CEditor::SwapListItems(ULONG ulListNum, ULONG ulFirstItem, ULONG ulSecondItem)
{
	if (!IsValidList(m_ulListNum)) return;

	HRESULT hRes = S_OK;
	SortListData* lpData1 = GetListRowData(ulListNum,ulFirstItem);
	SortListData* lpData2 = GetListRowData(ulListNum,ulSecondItem);

	// swap the data
	EC_B(m_lpControls[ulListNum].UI.lpList->List.SetItemData(ulFirstItem, (DWORD_PTR) lpData2));
	EC_B(m_lpControls[ulListNum].UI.lpList->List.SetItemData(ulSecondItem, (DWORD_PTR) lpData1));

	// swap the text (skip the first column!)
	CHeaderCtrl*	lpMyHeader = NULL;
	lpMyHeader = m_lpControls[ulListNum].UI.lpList->List.GetHeaderCtrl();
	if (lpMyHeader)
	{
		for (int i = 1;i<lpMyHeader->GetItemCount();i++)
		{
			CString szText1;
			CString szText2;

			szText1	= m_lpControls[ulListNum].UI.lpList->List.GetItemText(ulFirstItem,i);
			szText2	= m_lpControls[ulListNum].UI.lpList->List.GetItemText(ulSecondItem,i);
			WC_B(m_lpControls[ulListNum].UI.lpList->List.SetItemText(ulFirstItem,i,szText2));
			WC_B(m_lpControls[ulListNum].UI.lpList->List.SetItemText(ulSecondItem,i,szText1));
		}
	}
} // CEditor::SwapListItems

void CEditor::OnMoveListEntryUp(ULONG ulListNum)
{
	if (!IsValidList(m_ulListNum)) return;

	int	iItem = NULL;

	iItem = m_lpControls[ulListNum].UI.lpList->List.GetNextItem(-1,LVNI_FOCUSED | LVNI_SELECTED);
	DebugPrintEx(DBGGeneric,CLASS, _T("OnMoveListEntryUp"),_T("This item was selected: 0x%08X\n"),iItem);

	if (-1 == iItem) return;
	if (0 == iItem) return;

	SwapListItems(ulListNum,iItem-1, iItem);
	m_lpControls[ulListNum].UI.lpList->List.SetSelectedItem(iItem-1);
	m_lpControls[ulListNum].UI.lpList->bDirty = true;
} // CEditor::OnMoveListEntryUp

void CEditor::OnMoveListEntryDown(ULONG ulListNum)
{
	if (!IsValidList(m_ulListNum)) return;

	int	iItem = NULL;

	iItem = m_lpControls[ulListNum].UI.lpList->List.GetNextItem(-1,LVNI_FOCUSED | LVNI_SELECTED);
	DebugPrintEx(DBGGeneric,CLASS, _T("OnMoveListEntryDown"),_T("This item was selected: 0x%08X\n"),iItem);

	if (-1 == iItem) return;
	if (m_lpControls[ulListNum].UI.lpList->List.GetItemCount() == iItem+1) return;

	SwapListItems(ulListNum,iItem, iItem+1);
	m_lpControls[ulListNum].UI.lpList->List.SetSelectedItem(iItem+1);
	m_lpControls[ulListNum].UI.lpList->bDirty = true;
} // CEditor::OnMoveListEntryDown

void CEditor::OnMoveListEntryToTop(ULONG ulListNum)
{
	if (!IsValidList(m_ulListNum)) return;

	int	iItem = NULL;

	iItem = m_lpControls[ulListNum].UI.lpList->List.GetNextItem(-1,LVNI_FOCUSED | LVNI_SELECTED);
	DebugPrintEx(DBGGeneric,CLASS, _T("OnMoveListEntryToTop"),_T("This item was selected: 0x%08X\n"),iItem);

	if (-1 == iItem) return;
	if (0 == iItem) return;

	int i = 0;
	for (i = iItem ; i >0 ; i--)
	{
		SwapListItems(ulListNum,i, i-1);
	}
	m_lpControls[ulListNum].UI.lpList->List.SetSelectedItem(iItem);
	m_lpControls[ulListNum].UI.lpList->bDirty = true;
} // CEditor::OnMoveListEntryToTop

void CEditor::OnMoveListEntryToBottom(ULONG ulListNum)
{
	if (!IsValidList(m_ulListNum)) return;

	int	iItem = NULL;

	iItem = m_lpControls[ulListNum].UI.lpList->List.GetNextItem(-1,LVNI_FOCUSED | LVNI_SELECTED);
	DebugPrintEx(DBGGeneric,CLASS, _T("OnMoveListEntryDown"),_T("This item was selected: 0x%08X\n"),iItem);

	if (-1 == iItem) return;
	if (m_lpControls[ulListNum].UI.lpList->List.GetItemCount() == iItem+1) return;

	int i = 0;
	for (i = iItem ; i < m_lpControls[ulListNum].UI.lpList->List.GetItemCount() - 1 ; i++)
	{
		SwapListItems(ulListNum,i, i+1);
	}
	m_lpControls[ulListNum].UI.lpList->List.SetSelectedItem(iItem);
	m_lpControls[ulListNum].UI.lpList->bDirty = true;
} // CEditor::OnMoveListEntryToBottom

void CEditor::OnAddListEntry(ULONG ulListNum)
{
	if (!IsValidList(m_ulListNum)) return;

	int	iItem = m_lpControls[ulListNum].UI.lpList->List.GetItemCount();

	CString szTmp;
	szTmp.Format(_T("%d"),iItem); // STRING_OK

	SortListData* lpData = InsertListRow(ulListNum,iItem,szTmp);
	if (lpData) lpData->ulSortDataType = SORTLIST_MVPROP;

	m_lpControls[ulListNum].UI.lpList->List.SetSelectedItem(iItem);

	BOOL bDidEdit = OnEditListEntry(ulListNum);

	// if we didn't do anything in the edit, undo the add
	// pass false to make sure we don't mark the list dirty if it wasn't already
	if (!bDidEdit) OnDeleteListEntry(ulListNum, false);

	UpdateListButtons();
} // CEditor::OnAddListEntry

void CEditor::OnDeleteListEntry(ULONG ulListNum, BOOL bDoDirty)
{
	if (!IsValidList(m_ulListNum)) return;

	int	iItem = NULL;

	iItem = m_lpControls[ulListNum].UI.lpList->List.GetNextItem(-1,LVNI_FOCUSED | LVNI_SELECTED);
	DebugPrintEx(DBGGeneric,CLASS, _T("OnDeleteListEntry"),_T("This item was selected: 0x%08X\n"),iItem);

	if (iItem == -1) return;

	HRESULT hRes = S_OK;

	EC_B(m_lpControls[ulListNum].UI.lpList->List.DeleteItem(iItem));
	m_lpControls[ulListNum].UI.lpList->List.SetSelectedItem(iItem);

	if (S_OK == hRes && bDoDirty)
	{
		m_lpControls[ulListNum].UI.lpList->bDirty = true;
	}
	UpdateListButtons();
} // CEditor::OnDeleteListEntry

BOOL CEditor::OnEditListEntry(ULONG ulListNum)
{
	if (!IsValidList(m_ulListNum)) return false;

	int	iItem = NULL;

	iItem = m_lpControls[ulListNum].UI.lpList->List.GetNextItem(-1,LVNI_FOCUSED | LVNI_SELECTED);
	DebugPrintEx(DBGGeneric,CLASS, _T("OnEditListEntry"),_T("This item was selected: 0x%08X\n"),iItem);

	if (iItem == -1) return false;

	SortListData* lpData = GetListRowData(ulListNum,iItem);

	if (!lpData) return false;

	BOOL bDidEdit = DoListEdit(ulListNum,iItem,lpData);

	// the list is dirty now if the edit succeeded or it was already dirty
	m_lpControls[ulListNum].UI.lpList->bDirty = bDidEdit || ListDirty(ulListNum);
	return bDidEdit;
} // CEditor::OnEditListEntry

void CEditor::OnEditAction1()
{
	// Not Implemented
} // CEditor::OnEditAction1

void CEditor::OnEditAction2()
{
	// Not Implemented
} // CEditor::OnEditAction2

// Will be invoked on both edit button and double-click
// return true to indicate the entry was changed, false to indicate it was not
BOOL CEditor::DoListEdit(ULONG /*ulListNum*/, int /*iItem*/, SortListData* /*lpData*/)
{
	// Not Implemented
	return false;
} // CEditor::DoListEdit