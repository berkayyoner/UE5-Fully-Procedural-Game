/*************************************************************************
* ADOBE CONFIDENTIAL
* ___________________
*
*  Copyright 2015 Adobe Systems Incorporated
*  All Rights Reserved.
*
* NOTICE:  All information contained herein is, and remains
* the property of Adobe Systems Incorporated and its suppliers,
* if any.  The intellectual and technical concepts contained
* herein are proprietary to Adobe Systems Incorporated and its
* suppliers and are protected by all applicable intellectual property laws,
* including trade secret and or copyright laws.
* Dissemination of this information or reproduction of this material
* is strictly forbidden unless prior written permission is obtained
* from Adobe Systems Incorporated.
**************************************************************************/
import{useState,useEffect,useRef}from"react";export const useFABPillsFTE=(e,t)=>{const[l,s]=useState(!1),[o,c]=useState(!1),r=useRef(null);useEffect((()=>((async()=>{await initDcLocalStorage();const l=window.dcLocalStorage?.getItem("fabPillsFTEShown");l||(c(!0),s(!0),e(!0),t(!0),window.dcLocalStorage?.setItem("fabPillsFTEShown",!0),r.current=setTimeout((()=>{s(!1),c(!1),e(!1),t(!1),r.current=null}),15e3))})(),()=>{r.current&&clearTimeout(r.current)})),[e,t]);return{showPills:l,setShowPills:s,isFTE:o,setIsFTE:c,handlePillsClose:async()=>{s(!1),c(!1),e(!1),t(!1),r.current&&(clearTimeout(r.current),r.current=null),await initDcLocalStorage(),window.dcLocalStorage?.setItem("fabPillsFTEShown",!0)},pillsFTETimeoutRef:r}};